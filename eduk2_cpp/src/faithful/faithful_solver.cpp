#include "ukp/faithful_solver.hpp"
#include "ukp/dominance.hpp"
#include "eduk2_bounds.hpp"
#include "critical_sequence.hpp"
#include "preprocessing.hpp"
#include "incumbent.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace ukp::faithful {
namespace {

struct Cell {
    Profit profit = 0;
    int last_item = -1;
    Weight prev_capacity = 0;
};

constexpr std::size_t bound_type_index(BoundType type) {
    switch (type) {
        case BoundType::U3: return 0;
        case BoundType::V: return 1;
        case BoundType::TauStar: return 2;
        case BoundType::BestItemStar: return 3;
        case BoundType::Both: return 4;
    }
    return 4;
}

constexpr std::array<BoundType, 5> kBoundTypes{
    BoundType::U3, BoundType::V, BoundType::TauStar,
    BoundType::BestItemStar, BoundType::Both};

Profit run_core_bb_rec(const std::vector<Item>& core, Weight c, size_t k,
                       Profit cur_profit, Weight cur_weight,
                       Profit incumbent, long long& nodes, long long limit) {
    if (nodes >= limit || k >= core.size()) return std::max(incumbent, cur_profit);
    ++nodes;
    const Item& it = core[k];
    long long max_x = (c - cur_weight) / it.w;
    for (long long x = max_x; x >= 0; --x) {
        Profit np = cur_profit + safe_mul(x, it.p);
        Weight nw = cur_weight + x * it.w;
        incumbent = std::max(incumbent, np);
        if (k + 1 < core.size()) {
            Profit ub = np + floor_mul_div(c - nw, core[k + 1].p, core[k + 1].w);
            if (ub > incumbent) {
                incumbent = run_core_bb_rec(core, c, k + 1, np, nw, incumbent, nodes, limit);
            }
        }
        if (x == 0) break;
    }
    return incumbent;
}

[[maybe_unused]] Profit run_core_bb(std::vector<Item> items, Weight c, int requested_core,
                                    long long node_limit, long long& nodes) {
    std::sort(items.begin(), items.end(), better_ratio);
    const int default_core_size = std::min<int>(static_cast<int>(items.size()),
                                                std::max(100, static_cast<int>(items.size() / 100)));
    int core_size = requested_core > 0 ? requested_core : default_core_size;
    core_size = std::max(1, std::min<int>(core_size, static_cast<int>(items.size())));
    items.resize(static_cast<size_t>(core_size));
    nodes = 0;
    return run_core_bb_rec(items, c, 0, 0, 0, 0, nodes, node_limit);
}

struct CoreSearchResult {
    Profit profit = 0;
    std::vector<long long> multiplicity;
    long long nodes = 0;
    bool closed = false;
    long long multiple_removed = 0;
    long long modular_removed = 0;
};

struct EffectiveOptions {
    bool simple_dominance = false;
    bool core_remainder_ordering = false;
    bool modular_dominance = false;
    bool core_multiple_dominance = false;
};

EffectiveOptions effective_options(const SolverOptions& options) {
    if (options.paper_faithful_mode) return {};
    return {options.use_simple_dominance, options.use_core_remainder_ordering,
            options.use_modular_dominance, options.use_core_multiple_dominance};
}

void core_search(const std::vector<Item>& core, const BoundContext& bounds, BoundPolicy policy,
                 Weight capacity, Profit global_upper, long long node_limit,
                 std::size_t k, Weight used_weight, Profit used_profit,
                 std::vector<long long>& current, CoreSearchResult& result) {
    if (result.closed || result.nodes >= node_limit) return;
    ++result.nodes;
    if (used_profit > result.profit) {
        result.profit = used_profit;
        result.multiplicity = current;
        if (result.profit >= global_upper) {
            result.closed = true;
            return;
        }
    }
    if (k == core.size()) return;
    const Item& item = core[k];
    const Weight remaining = capacity - used_weight;
    for (long long count = remaining / item.w; count >= 0; --count) {
        const Weight next_weight = used_weight + safe_mul(count, item.w);
        const Profit next_profit = safe_add(used_profit, safe_mul(count, item.p));
        const Profit upper = safe_add(next_profit,
            compute_bound(bounds, capacity - next_weight, policy).upper);
        if (upper > result.profit) {
            current[static_cast<std::size_t>(item.id)] = count;
            core_search(core, bounds, policy, capacity, global_upper, node_limit, k + 1,
                        next_weight, next_profit, current, result);
            current[static_cast<std::size_t>(item.id)] = 0;
        }
        if (count == 0 || result.closed || result.nodes >= node_limit) break;
    }
}

[[maybe_unused]] CoreSearchResult run_pyasukp_core_bb(std::vector<Item> items, const BoundContext& bounds,
                                                       Weight capacity, Profit global_upper,
                                                       int requested_core, long long node_limit,
                                                       std::size_t original_count) {
    std::sort(items.begin(), items.end(), better_ratio);
    const int default_size = std::min<int>(items.size(), std::max(500, static_cast<int>(items.size() / 100)));
    items.resize(static_cast<std::size_t>(std::max(1, std::min(requested_core > 0 ? requested_core : default_size,
                                                                static_cast<int>(items.size())))));
    CoreSearchResult result;
    result.multiplicity.assign(original_count, 0);
    std::vector<long long> current(original_count, 0);
    core_search(items, bounds, BoundPolicy::BestCertified, capacity, global_upper, std::max<long long>(0, node_limit),
                0, 0, 0, current, result);
    return result;
}

// Structural C++ counterpart of bandbukp2.ml: greedy_fill establishes the
// incumbent, complete explores a completion, and traverse controls the node
// budget.  The state table is the Dynefflist equivalent: for equal used weight
// only the greatest profit can lead to a useful residual subproblem.
struct CoreTraversal {
    const std::vector<Item>& items;
    const BoundContext& bounds;
    BoundPolicy policy;
    Weight capacity;
    Profit global_upper;
    long long limit;
    CoreSearchResult result;
    std::unordered_map<Weight, Profit> best_profit_at_weight;
    std::vector<Weight> min_remaining_weight;
};

std::vector<Weight> lightest_worse(const std::vector<Item>& items) {
    std::vector<Weight> minweights(items.size(), 0);
    if (items.size() < 2) return minweights;
    minweights[items.size() - 2] = items[items.size() - 1].w;
    for (std::size_t i = items.size() - 2; i > 0; --i) {
        minweights[i - 1] = std::min(items[i].w, minweights[i]);
    }
    return minweights;
}

bool remember_core_state(CoreTraversal& search, Weight used_weight, Profit used_profit) {
    const auto known = search.best_profit_at_weight.find(used_weight);
    if (known != search.best_profit_at_weight.end() && known->second >= used_profit) return false;
    search.best_profit_at_weight[used_weight] = used_profit;
    return true;
}

void record_core_solution(CoreTraversal& search, Profit profit,
                          const std::vector<long long>& multiplicity) {
    if (profit > search.result.profit) {
        search.result.profit = profit;
        search.result.multiplicity = multiplicity;
    }
    if (search.result.profit >= search.global_upper) search.result.closed = true;
}

void complete(CoreTraversal& search, std::size_t item_index, Weight used_weight,
              Profit used_profit, std::vector<long long>& multiplicity) {
    if (search.result.closed || search.result.nodes >= search.limit) return;
    ++search.result.nodes;
    if (!remember_core_state(search, used_weight, used_profit)) return;
    record_core_solution(search, used_profit, multiplicity);
    if (item_index + 1 >= search.items.size()) return;

    const Weight remaining = search.capacity - used_weight;
    if (item_index < search.min_remaining_weight.size() &&
        remaining < search.min_remaining_weight[item_index]) {
        return;
    }

    const Item& item = search.items[item_index];
    for (long long count = remaining / item.w; count >= 0; --count) {
        const Weight next_weight = used_weight + safe_mul(count, item.w);
        const Profit next_profit = safe_add(used_profit, safe_mul(count, item.p));
        const Profit residual_upper = compute_bound(search.bounds, search.capacity - next_weight, search.policy).upper;
        if (safe_add(next_profit, residual_upper) > search.result.profit) {
            multiplicity[static_cast<std::size_t>(item.id)] = count;
            complete(search, item_index + 1, next_weight, next_profit, multiplicity);
            multiplicity[static_cast<std::size_t>(item.id)] = 0;
        }
        if (count == 0 || search.result.closed || search.result.nodes >= search.limit) break;
    }
}

void greedy_fill(CoreTraversal& search, std::vector<long long>& multiplicity) {
    Weight used_weight = 0;
    Profit profit = 0;
    for (const Item& item : search.items) {
        const long long count = (search.capacity - used_weight) / item.w;
        if (count == 0) continue;
        for (long long copy = 0; copy < count; ++copy) {
            multiplicity[static_cast<std::size_t>(item.id)] += 1;
            used_weight += item.w;
            profit = safe_add(profit, item.p);
            remember_core_state(search, used_weight, profit);
        }
    }
    record_core_solution(search, profit, multiplicity);
    std::fill(multiplicity.begin(), multiplicity.end(), 0);
}

// `backtrack` is the MTU/PYAsUKP branch controller.  Multiplicity choices are
// undone by complete after every child, so the same vector can be reused.
void backtrack(CoreTraversal& search, std::vector<long long>& multiplicity) {
    complete(search, 0, 0, 0, multiplicity);
}

CoreSearchResult traverse_core(const std::vector<Item>& dp_items, const BoundContext& bounds, BoundPolicy policy,
                               Weight capacity, Profit global_upper, int requested_core,
                               long long limit, std::size_t original_count,
                               const EffectiveOptions& effective, bool paper_faithful_mode,
                               std::vector<int>* selected_core_ids = nullptr) {
    std::vector<Item> core_items;
    if (paper_faithful_mode) {
        // dp_items is already globally reduced and sorted by better_ratio.
        // The faithful core is exactly its prefix: no local ordering or
        // filtering is permitted on this path.
        const std::size_t n = dp_items.size();
        const std::size_t core_size = std::min(n, std::max<std::size_t>(100, n / 100));
        core_items.assign(dp_items.begin(), dp_items.begin() + core_size);
    } else {
        core_items = dp_items;
        const int default_size = std::min<int>(core_items.size(), std::max(100, static_cast<int>(core_items.size() / 100)));
        const int core_size = std::max(1, std::min(requested_core > 0 ? requested_core : default_size,
                                                    static_cast<int>(core_items.size())));
        if (effective.core_remainder_ordering) {
            std::sort(core_items.begin(), core_items.end(), [capacity](const Item& left, const Item& right) {
                const Weight left_remainder = capacity % left.w;
                const Weight right_remainder = capacity % right.w;
                if (left_remainder != right_remainder) return left_remainder < right_remainder;
                return better_ratio(left, right);
            });
        } else {
            std::sort(core_items.begin(), core_items.end(), better_ratio);
        }
        core_items.resize(static_cast<std::size_t>(core_size));
    }
    if (selected_core_ids != nullptr) {
        selected_core_ids->clear();
        selected_core_ids->reserve(core_items.size());
        for (const Item& item : core_items) selected_core_ids->push_back(item.id);
    }

    std::vector<Item> filtered = core_items;
    long long core_multiple_removed = 0;
    long long core_modular_removed = 0;
    if (!paper_faithful_mode) {
        filtered.clear();
        // Core-local reductions are experimental and apply only to this copy.
        for (const Item& candidate : core_items) {
            bool dominated = false;
            bool modular = false;
            for (const Item& kept : filtered) {
                if (effective.core_multiple_dominance) {
                    const long long copies = candidate.w / kept.w;
                    if (copies > 0 && safe_mul(copies, kept.p) >= candidate.p) { dominated = true; break; }
                }
                if (effective.modular_dominance && kept.w <= candidate.w && bounds.best.w != candidate.w) {
                    const Weight candidate_remainder = candidate.w % bounds.best.w;
                    const Weight kept_remainder = kept.w % bounds.best.w;
                    const Profit kept_z = safe_add(safe_mul(kept.w, bounds.best.p),
                                                    -safe_mul(bounds.best.w, kept.p));
                    const Profit candidate_z = safe_add(safe_mul(candidate.w, bounds.best.p),
                                                         -safe_mul(bounds.best.w, candidate.p));
                    if (candidate_remainder == 0 ||
                        (candidate_remainder == kept_remainder && kept_z <= candidate_z)) {
                        dominated = true;
                        modular = true;
                        break;
                    }
                }
            }
            if (!dominated) {
                filtered.push_back(candidate);
            } else if (modular) {
                ++core_modular_removed;
            } else {
                ++core_multiple_removed;
            }
        }
    }
    // Bounds in the core must describe its locally filtered items, while the
    // global upper remains the global certificate used for closure.
    BoundContext core_bounds = make_bound_context(filtered);
    CoreTraversal search{filtered, core_bounds, policy, capacity, global_upper, std::max<long long>(0, limit), {}, {},
                         lightest_worse(filtered)};
    search.result.multiple_removed = core_multiple_removed;
    search.result.modular_removed = core_modular_removed;
    search.result.multiplicity.assign(original_count, 0);
    std::vector<long long> multiplicity(original_count, 0);
    greedy_fill(search, multiplicity);
    backtrack(search, multiplicity);
    return search.result;
}

Solution solution_from_best_item(const Instance& inst, const Item& best, long long count) {
    Solution sol;
    sol.profit = safe_mul(count, best.p);
    sol.weight = count * best.w;
    sol.optimal = true;
    sol.solver_name = "faithful";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);
    if (best.id >= 0 && static_cast<std::size_t>(best.id) < sol.multiplicity_by_id.size()) {
        sol.multiplicity_by_id[static_cast<std::size_t>(best.id)] = count;
    }
    return sol;
}

}  // namespace

Solver::Solver(SolverOptions options) : options_(options) {}

SolverResult Solver::solve(const Instance& inst) {
    if (inst.capacity < 0) throw std::invalid_argument("negative capacity");
    SolverResult result;
    const EffectiveOptions effective = effective_options(options_);
    result.stats.original_items = static_cast<long long>(inst.items.size());
    if (inst.items.empty() || inst.capacity == 0) {
        result.solution.multiplicity_by_id.assign(inst.items.size(), 0);
        result.solution.optimal = true;
        result.solution.solver_name = "faithful";
        result.stats.stop_reason = "empty_instance";
        result.stats.dp_stop_reason = "empty_instance";
        return result;
    }

    detail::PreprocessResult preprocessing = detail::preprocess_items(inst, effective.simple_dominance);
    std::vector<Item> items = std::move(preprocessing.items);
    result.stats.items_removed_simple = preprocessing.simple_removed;
    result.stats.items_removed_multiple = preprocessing.multiple_removed;
    result.stats.after_preprocess_items = static_cast<long long>(items.size());

    BoundContext ctx = make_bound_context(items);
    BoundValue global_bound{};
    global_bound.upper = std::numeric_limits<Profit>::max();
    long long best_count = inst.capacity / ctx.best.w;
    Profit incumbent = safe_mul(best_count, ctx.best.p);
    if (options_.use_bounds) {
        const detail::BoundPhase bound_phase = detail::initialize_bounds(items, inst.capacity, options_.bound_policy);
        ctx = bound_phase.context;
        global_bound = bound_phase.global;
        best_count = bound_phase.best_count;
        incumbent = bound_phase.incumbent;
        ++result.stats.bound_calls;
        result.stats.global_bound_used = bound_type_name(global_bound.type);
        result.stats.bound_winner = result.stats.global_bound_used;
    }

    detail::Incumbent incumbent_solution(inst.items.size());
    incumbent_solution.consider(incumbent, safe_mul(best_count, ctx.best.w),
                                solution_from_best_item(inst, ctx.best, best_count).multiplicity_by_id,
                                detail::no_point, ctx.best.id, best_count);

    // EDUK2 bound-stop shortcut.  This is exact only when the certified incumbent
    // is the one reconstructed here, so it is applied before the core B&B changes
    // the incumbent without storing its multiplicities.
    if (options_.use_bounds && incumbent >= global_bound.upper) {
        result.solution = solution_from_best_item(inst, ctx.best, best_count);
        result.stats.active_items_final = static_cast<long long>(items.size());
        result.stats.stop_reason = "initial_bound";
        return result;
    }

    if (options_.use_bounds) {
        items = detail::reduce_variables_by_bound(items, ctx, inst.capacity, incumbent,
                                                  result.stats.bound_calls, options_.bound_policy);
        result.stats.items_removed_bound = result.stats.after_preprocess_items - static_cast<long long>(items.size());
        std::sort(items.begin(), items.end(), better_ratio);
        ctx = make_bound_context(items);
        result.stats.after_preprocess_items = static_cast<long long>(items.size());
    }

    // This is the global post-reduction list used by the DP.  The core B&B
    // only receives a const view and performs every experimental reduction on
    // its own local copy.
    const std::vector<Item> dp_items = items;
    result.stats.dp_item_ids.reserve(dp_items.size());
    for (const Item& item : dp_items) result.stats.dp_item_ids.push_back(item.id);

    if (options_.use_core_bb) {
        constexpr long long kFaithfulCoreNodeLimit = 10'000;
        const long long core_node_limit = options_.paper_faithful_mode
            ? kFaithfulCoreNodeLimit : options_.bb_node_limit;
        result.stats.core_node_limit = core_node_limit;
        CoreSearchResult core = traverse_core(dp_items, ctx, options_.bound_policy, inst.capacity,
                                               global_bound.upper, options_.core_size,
                                               core_node_limit, inst.items.size(), effective,
                                               options_.paper_faithful_mode, &result.stats.core_item_ids);
        incumbent = std::max(incumbent, core.profit);
        if (core.profit > incumbent_solution.profit) ++result.stats.incumbent_improvements_bb;
        Weight core_weight = 0;
        for (const Item& item : inst.items) {
            core_weight += safe_mul(core.multiplicity[static_cast<std::size_t>(item.id)], item.w);
        }
        incumbent_solution.consider(core.profit, core_weight, core.multiplicity);
        result.stats.bb_nodes += core.nodes;
        result.stats.items_removed_core_multiple += core.multiple_removed;
        result.stats.items_removed_modular += core.modular_removed;
        if (core.closed) {
            result.solution = incumbent_solution.solution("faithful");
            result.stats.active_items_final = static_cast<long long>(dp_items.size());
            result.stats.stop_reason = "core_bound_closed";
            return result;
        }
    }

    // Select.next_lightest traverses the residual items by nondecreasing
    // weight.  Equal-weight candidates retain the ratio/id order already
    // prescribed by better_ratio, so an inferior duplicate is never made
    // active before its dominating peer.
    std::vector<Item> items_by_weight;
    items_by_weight.reserve(dp_items.size());
    for (const Item& item : dp_items) {
        if (item.w <= inst.capacity) items_by_weight.push_back(item);
    }
    std::stable_sort(items_by_weight.begin(), items_by_weight.end(),
        [](const Item& left, const Item& right) {
            if (left.w != right.w) return left.w < right.w;
            return better_ratio(left, right);
        });
    std::size_t next_item = 0;

    // `items` now has the same role as PYAsUKP's decreasingS: it contains
    // only introduced, threshold-undominated items and stays ratio ordered.
    items.clear();

    // The sequence is the DP representation: it stores only strict increases
    // of f(N, y), in topological weight order.
    detail::CriticalSequence sequence;
    sequence.configure_item_order(dp_items);
    std::unordered_map<int, Weight> last_contribution;
    const Weight wmin = items_by_weight.empty() ? Weight{1} : items_by_weight.front().w;
    // PYAsUKP's executable defaults layer_height to 100 and then takes the
    // maximum with the lightest weight during reduction.
    Weight h = options_.slice_height > 0 ? options_.slice_height : std::max<Weight>(100, wmin);
    if (h <= 0) h = 1;
    const Weight half_capacity = (inst.capacity + 1) / 2;
    const Weight introduction_limit = items_by_weight.empty() ? Weight{0} : items_by_weight.back().w;
    // EDUK first finishes its reduction through the heaviest candidate item;
    // EDUK2 then reaches c/2 and extends once by the heaviest item that
    // survived threshold dominance.
    const Weight initial_process_limit = std::max(half_capacity, introduction_limit);
    const Weight candidate_limit = inst.capacity;
    Weight process_limit = initial_process_limit;
    bool half_capacity_extension_done = false;
    bool closed_by_bound = false;
    std::array<long long, kBoundTypes.size()> contextual_bound_counts{};
    auto record_contextual_bound = [&](BoundType type, SliceStats& slice) {
        ++contextual_bound_counts[bound_type_index(type)];
        const char* name = bound_type_name(type);
        if (slice.contextual_bound_used != name) slice.contextual_bound_used = name;
    };
    auto publish_contextual_bound_counts = [&]() {
        for (std::size_t index = 0; index < kBoundTypes.size(); ++index) {
            if (contextual_bound_counts[index] != 0) {
                result.stats.contextual_bound_calls[bound_type_name(kBoundTypes[index])] =
                    contextual_bound_counts[index];
            }
        }
    };

    // Most greedy completions do not improve the incumbent.  Keep one
    // reconstruction buffer for the rare candidates that do, and remember the
    // positions written so clearing it does not become another O(n) pass.
    std::vector<long long> reconstruction_multiplicity;
    std::vector<int> reconstruction_touched_ids;
    reconstruction_touched_ids.reserve(dp_items.size());

    // Greedy completion retains the active-item order.  This suffix minimum
    // only lets it stop once no remaining item can fit; it never changes
    // which item would be considered next.
    std::vector<Weight> active_suffix_min_weight;
    auto rebuild_active_suffix_min_weight = [&]() {
        active_suffix_min_weight.resize(items.size());
        if (items.empty()) return;
        Weight minimum_weight = items.back().w;
        for (std::size_t index = items.size(); index-- > 0;) {
            minimum_weight = std::min(minimum_weight, items[index].w);
            active_suffix_min_weight[index] = minimum_weight;
        }
    };
    rebuild_active_suffix_min_weight();

    auto ensure_active_suffix_min_weight = [&]() {
        if (active_suffix_min_weight.size() != items.size()) {
            rebuild_active_suffix_min_weight();
        }
    };

    auto consider_greedy_completion = [&](detail::PointId state_index) {
        ensure_active_suffix_min_weight();
        const detail::State& state = sequence.state(state_index);
        Weight used_weight = state.weight;
        Weight remaining = inst.capacity - used_weight;
        Profit candidate_profit = state.profit;
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (remaining < active_suffix_min_weight[index]) break;
            const Item& item = items[index];
            if (item.w > remaining) continue;
            const long long copies = remaining / item.w;
            const Weight added_weight = safe_mul(copies, item.w);
            used_weight += added_weight;
            remaining -= added_weight;
            candidate_profit = safe_add(candidate_profit, safe_mul(copies, item.p));
        }

        // Keep this predicate in sync with Incumbent::consider.  In particular,
        // a tied profit only wins when it uses more weight.
        if (candidate_profit < incumbent_solution.profit ||
            (candidate_profit == incumbent_solution.profit &&
             used_weight <= incumbent_solution.weight)) {
            return;
        }

        if (reconstruction_multiplicity.empty()) {
            reconstruction_multiplicity.assign(inst.items.size(), 0);
        }
        for (detail::PointId cursor = state_index; cursor != detail::no_point;
             cursor = sequence.state(cursor).predecessor) {
            const int item_id = sequence.state(cursor).item_id;
            if (item_id < 0) break;
            long long& count = reconstruction_multiplicity[static_cast<std::size_t>(item_id)];
            if (count == 0) reconstruction_touched_ids.push_back(item_id);
            ++count;
        }
        Weight reconstruction_weight = state.weight;
        Weight reconstruction_remaining = inst.capacity - reconstruction_weight;
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (reconstruction_remaining < active_suffix_min_weight[index]) break;
            const Item& item = items[index];
            if (item.w > reconstruction_remaining) continue;
            const long long copies = reconstruction_remaining / item.w;
            long long& count = reconstruction_multiplicity[static_cast<std::size_t>(item.id)];
            if (count == 0) reconstruction_touched_ids.push_back(item.id);
            count += copies;
            const Weight added_weight = safe_mul(copies, item.w);
            reconstruction_weight += added_weight;
            reconstruction_remaining -= added_weight;
        }

        if (incumbent_solution.consider(candidate_profit, used_weight, reconstruction_multiplicity)) {
            incumbent = incumbent_solution.profit;
            ++result.stats.incumbent_improvements_dp;
        }
        for (const int item_id : reconstruction_touched_ids) {
            reconstruction_multiplicity[static_cast<std::size_t>(item_id)] = 0;
        }
        reconstruction_touched_ids.clear();
    };

    // Listing 1 mapping: build/process a slice; fathom its states with
    // f(y)+U(c-y)<=z; greedily complete survivors; update contributions;
    // apply threshold dominance at the completed boundary; then test stopping.
    // After crossing c/2, extend once through the largest active-item range,
    // as in EDUK2's `standard` recurrence.
    for (Weight ya = 0; ya < process_limit;) {
        const Weight yb = std::min(process_limit, ya + h);
        SliceStats slice;
        slice.begin = ya;
        slice.end = yb;
        slice.active_items_before = static_cast<long long>(items.size());
        const detail::SliceBuildResult build = sequence.process_slice_incremental(
            ya, yb, candidate_limit, items, items_by_weight, next_item,
            [&](const Item& item, Profit) {
                if (options_.use_bounds) {
                    const BoundValue residual = compute_bound(
                        ctx, inst.capacity - item.w, options_.bound_policy);
                    ++result.stats.bound_calls;
                    record_contextual_bound(residual.type, slice);
                    if (safe_add(item.p, residual.upper) <= incumbent) return false;
                }
                last_contribution[item.id] = item.w;
                return true;
            },
            [&](detail::PointId state_index) {
            const detail::State& state = sequence.state(state_index);
            ++slice.states_entered;
            if (options_.use_bounds) {
                const BoundValue residual = compute_bound(ctx, inst.capacity - state.weight, options_.bound_policy);
                ++result.stats.bound_calls;
                record_contextual_bound(residual.type, slice);
                if (safe_add(state.profit, residual.upper) <= incumbent) {
                    ++result.stats.states_fathomed;
                    ++slice.states_fathomed_by_bound;
                    return false;
                }
            }
            // Listing 1's fathoming improves z by greedily completing every
            // surviving optimal state with the current dominance-free items.
            consider_greedy_completion(state_index);
            // Every state exposed by the sequence is a strict skip-point.
            if (state.item_id >= 0) last_contribution[state.item_id] = state.weight;
            ++result.stats.states_kept;
            ++slice.states_kept;
            return true;
        });
        result.stats.states_scanned += build.successor_attempts;
        result.stats.successor_item_scans += build.successor_item_scans;
        result.stats.backfill_attempts += build.backfill_attempts;
        result.stats.items_considered_for_introduction += build.items_considered_for_introduction;
        result.stats.items_introduced += build.items_introduced;
        result.stats.items_rejected_by_envelope += build.items_rejected_by_envelope;
        result.stats.items_rejected_by_bound += build.items_rejected_by_bound;
        slice.successor_attempts += build.successor_attempts;
        slice.successor_item_scans += build.successor_item_scans;
        slice.backfill_attempts += build.backfill_attempts;
        slice.states_created += build.states_created;
        slice.items_considered_for_introduction += build.items_considered_for_introduction;
        slice.items_introduced += build.items_introduced;
        slice.items_rejected_by_envelope += build.items_rejected_by_envelope;
        slice.items_rejected_by_bound += build.items_rejected_by_bound;
        result.stats.points_generated += build.points_generated;
        if (options_.use_bounds && incumbent >= global_bound.upper) {
            closed_by_bound = true;
            slice.active_items_after = static_cast<long long>(items.size());
            result.stats.slices.push_back(std::move(slice));
            break;
        }

        // Dynamic threshold dominance from EDUK/PYAsUKP:
        // last_contribution(i) + w_i <= yb.  Run it only at a completed
        // slice, and keep one item so the active recurrence is never empty.
        if (!items.empty()) {
            std::vector<Item> next_items;
            next_items.reserve(items.size());
            std::size_t remaining_active = items.size();
            for (const Item& item : items) {
                const auto contribution = last_contribution.find(item.id);
                if (contribution == last_contribution.end()) {
                    throw std::logic_error("active item has no introduction contribution");
                }
                if (remaining_active > 1 &&
                    safe_add(contribution->second, item.w) <= yb) {
                    --remaining_active;
                    ++result.stats.items_removed_threshold;
                    ++slice.items_removed_threshold;
                } else {
                    next_items.push_back(item);
                }
            }
            // Threshold removal changes the active recurrence.  Rebuild the
            // greedy suffix now; the valid residual bound context is rebuilt
            // below together with the not-yet-introduced candidates.
            if (next_items.size() != items.size()) {
                items = std::move(next_items);
                rebuild_active_suffix_min_weight();
            }
        }
        if (build.items_considered_for_introduction > 0 || slice.items_removed_threshold > 0) {
            // Remove collectively/context/threshold dominated items from the
            // bound instance, but retain every item whose introduction weight
            // has not been reached yet.  This keeps the bound valid while it
            // tightens monotonically toward the active residual problem.
            std::vector<Item> residual_items = items;
            residual_items.insert(residual_items.end(),
                                  items_by_weight.begin() + static_cast<std::ptrdiff_t>(next_item),
                                  items_by_weight.end());
            if (!residual_items.empty()) ctx = make_bound_context(residual_items);
        }
        slice.active_items_after = static_cast<long long>(items.size());
        result.stats.slices.push_back(std::move(slice));
        const std::size_t active_count = items.size();
        if (!half_capacity_extension_done && yb >= initial_process_limit) {
            const Weight active_wmax = items.empty() ? Weight{0} :
                std::max_element(items.begin(), items.end(),
                    [](const Item& left, const Item& right) { return left.w < right.w; })->w;
            process_limit = std::min(inst.capacity, safe_add(yb, active_wmax));
            half_capacity_extension_done = true;
        }
        if (active_count == 1 && half_capacity_extension_done && yb >= process_limit) break;
        ya = yb;
    }

    // c/2 cut: the sequence query is the prefix maximum, because its profits
    // are strictly increasing with its skip-point weights.
    detail::PointId first_index = 0;
    detail::PointId second_index = 0;
    Profit split_profit = 0;
    for (const detail::PointId index : sequence.skip_points()) {
        const detail::State& state = sequence.state(index);
        const detail::PointId partner = sequence.state_at_or_before(inst.capacity - state.weight);
        const Profit candidate = safe_add(state.profit, sequence.state(partner).profit);
        if (candidate > split_profit) {
            split_profit = candidate;
            first_index = index;
            second_index = partner;
        }
    }

    if (split_profit <= incumbent_solution.profit) {
        publish_contextual_bound_counts();
        result.solution = incumbent_solution.solution("faithful");
        result.stats.estimated_state_bytes = static_cast<long long>(sequence.estimated_bytes());
        const std::size_t active_count = items.size();
        result.stats.active_items_final = static_cast<long long>(active_count);
        result.stats.dp_stop_reason = closed_by_bound ? "bound_closed" :
            (active_count == 1 ? "single_active_item" : "half_capacity_cut");
        result.stats.stop_reason = closed_by_bound ? "dp_bound_closed" :
            (active_count == 1 ? "single_item" : "half_capacity");
        return result;
    }

    Solution sol;
    sol.profit = split_profit;
    sol.weight = 0;
    sol.optimal = true;
    sol.solver_name = "faithful";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);

    // Solutions and the DP reconstruction are indexed by item ID.  Build the
    // direct lookup once, retaining the first matching item just as find_if
    // did when duplicate IDs are supplied.
    std::vector<Weight> item_weight_by_id(inst.items.size());
    std::vector<bool> item_id_present(inst.items.size(), false);
    for (const Item& item : inst.items) {
        if (item.id < 0 || static_cast<std::size_t>(item.id) >= item_weight_by_id.size()) {
            throw std::runtime_error("backtracking failed");
        }
        const std::size_t item_index = static_cast<std::size_t>(item.id);
        if (!item_id_present[item_index]) {
            item_weight_by_id[item_index] = item.w;
            item_id_present[item_index] = true;
        }
    }

    auto add_trace = [&](detail::PointId index) {
      while (index != detail::no_point) {
        const detail::State& state = sequence.state(index);
        const int item_id = state.item_id;
        if (item_id < 0) break;
        if (static_cast<std::size_t>(item_id) >= item_weight_by_id.size() ||
            !item_id_present[static_cast<std::size_t>(item_id)]) {
            throw std::runtime_error("backtracking failed");
        }
        sol.multiplicity_by_id[static_cast<std::size_t>(item_id)]++;
        sol.weight += item_weight_by_id[static_cast<std::size_t>(item_id)];
        index = state.predecessor;
      }
    };
    add_trace(first_index);
    add_trace(second_index);

    result.solution = std::move(sol);
    publish_contextual_bound_counts();
    result.stats.estimated_state_bytes = static_cast<long long>(sequence.estimated_bytes());
    const std::size_t active_count = items.size();
    result.stats.active_items_final = static_cast<long long>(active_count);
    result.stats.dp_stop_reason = closed_by_bound ? "bound_closed" :
        (active_count == 1 ? "single_active_item" : "half_capacity_cut");
    result.stats.stop_reason = closed_by_bound ? "dp_bound_closed" :
        (active_count == 1 ? "single_item" : "half_capacity");
    return result;
}

}  // namespace ukp::faithful
