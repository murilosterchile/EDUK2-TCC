#include "ukp/faithful_solver.hpp"
#include "ukp/dominance.hpp"
#include "eduk2_bounds.hpp"
#include "critical_sequence.hpp"
#include "preprocessing.hpp"
#include "incumbent.hpp"
#include <algorithm>
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
    const int default_size = std::min<int>(items.size(), std::max(100, static_cast<int>(items.size() / 100)));
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
};

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
    const auto known = search.best_profit_at_weight.find(used_weight);
    if (known != search.best_profit_at_weight.end() && known->second >= used_profit) return;
    search.best_profit_at_weight[used_weight] = used_profit;
    record_core_solution(search, used_profit, multiplicity);
    if (item_index == search.items.size()) return;

    const Item& item = search.items[item_index];
    const Weight remaining = search.capacity - used_weight;
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
        multiplicity[static_cast<std::size_t>(item.id)] += count;
        used_weight += safe_mul(count, item.w);
        profit = safe_add(profit, safe_mul(count, item.p));
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
    CoreTraversal search{filtered, core_bounds, policy, capacity, global_upper, std::max<long long>(0, limit), {}, {}};
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

    // The DP may maintain a changing active set for its recurrence, but it
    // starts from the unchanged global list selected above.
    items = dp_items;

    // The sequence is the DP representation: it stores only strict increases
    // of f(N, y), in topological weight order.
    detail::CriticalSequence sequence;
    std::unordered_map<int, Weight> last_contribution;
    Weight wmin = std::min_element(items.begin(), items.end(),
        [](const Item& a, const Item& b) { return a.w < b.w; })->w;
    Weight h = options_.slice_height > 0 ? options_.slice_height : wmin;
    if (h <= 0) h = 1;
    const Weight half_capacity = (inst.capacity + 1) / 2;
    // EDUK2 first computes through c/2, then extends the recurrence once by
    // the largest item still active after threshold dominance.  Candidates
    // must nevertheless be retained up to c before that extension is known.
    const Weight candidate_limit = inst.capacity;
    Weight process_limit = half_capacity;
    bool half_capacity_extension_done = false;
    bool closed_by_bound = false;

    auto consider_greedy_completion = [&](detail::PointId state_index) {
        const detail::State& state = sequence.state(state_index);
        std::vector<long long> multiplicity(inst.items.size(), 0);
        for (detail::PointId cursor = state_index; cursor != detail::no_point;
             cursor = sequence.state(cursor).predecessor) {
            const int item_id = sequence.state(cursor).item_id;
            if (item_id < 0) break;
            ++multiplicity[static_cast<std::size_t>(item_id)];
        }

        Weight used_weight = state.weight;
        Profit candidate_profit = state.profit;
        for (const Item& item : items) {
            const long long copies = (inst.capacity - used_weight) / item.w;
            if (copies == 0) continue;
            multiplicity[static_cast<std::size_t>(item.id)] += copies;
            used_weight += safe_mul(copies, item.w);
            candidate_profit = safe_add(candidate_profit, safe_mul(copies, item.p));
        }
        if (incumbent_solution.consider(candidate_profit, used_weight, std::move(multiplicity))) {
            incumbent = incumbent_solution.profit;
            ++result.stats.incumbent_improvements_dp;
        }
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
        const detail::SliceBuildResult build = sequence.process_slice(
            ya, yb, candidate_limit, items, [&](detail::PointId state_index) {
            const detail::State& state = sequence.state(state_index);
            ++slice.states_entered;
            if (options_.use_bounds) {
                const BoundValue residual = compute_bound(ctx, inst.capacity - state.weight, options_.bound_policy);
                ++result.stats.bound_calls;
                ++result.stats.contextual_bound_calls[bound_type_name(residual.type)];
                slice.contextual_bound_used = bound_type_name(residual.type);
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
        slice.successor_attempts += build.successor_attempts;
        slice.states_created += build.states_created;
        result.stats.points_generated += build.points_generated;
        if (options_.use_bounds && incumbent >= global_bound.upper) {
            closed_by_bound = true;
            slice.active_items_after = slice.active_items_before;
            result.stats.slices.push_back(std::move(slice));
            break;
        }

        // Dynamic threshold dominance from EDUK/PYAsUKP:
        // last_contribution(i) + w_i <= yb.  Run it only at a completed
        // slice, and keep one item so the active recurrence is never empty.
        const Weight introduced_through = std::max_element(items.begin(), items.end(),
            [](const Item& left, const Item& right) { return left.w < right.w; })->w;
        if (yb < introduced_through) {
            slice.active_items_after = slice.active_items_before;
            result.stats.slices.push_back(std::move(slice));
        } else {
            std::vector<Item> next_items;
            next_items.reserve(items.size());
            std::size_t remaining_active = items.size();
            for (const Item& item : items) {
                if (remaining_active > 1 &&
                    safe_add(last_contribution[item.id], item.w) <= yb) {
                    --remaining_active;
                    ++result.stats.items_removed_threshold;
                    ++slice.items_removed_threshold;
                } else {
                    next_items.push_back(item);
                }
            }
            // Threshold removal changes the actual residual instance. Rebuild
            // so q* certification and BestCertified selection stay current.
            if (next_items.size() != items.size()) {
                items = std::move(next_items);
                ctx = make_bound_context(items);
            }
            slice.active_items_after = static_cast<long long>(items.size());
            result.stats.slices.push_back(std::move(slice));
        }
        const std::size_t active_count = items.size();
        if (!half_capacity_extension_done && yb >= half_capacity) {
            const Weight active_wmax = std::max_element(items.begin(), items.end(),
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

    Solution sol;
    sol.profit = split_profit;
    sol.weight = 0;
    sol.optimal = true;
    sol.solver_name = "faithful";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);

    auto add_trace = [&](detail::PointId index) {
      while (index != detail::no_point) {
        const detail::State& state = sequence.state(index);
        const int item_id = state.item_id;
        if (item_id < 0) break;
        const auto it = std::find_if(inst.items.begin(), inst.items.end(),
            [&](const Item& x) { return x.id == item_id; });
        if (it == inst.items.end()) throw std::runtime_error("backtracking failed");
        sol.multiplicity_by_id[static_cast<size_t>(it->id)]++;
        sol.weight += it->w;
        index = state.predecessor;
      }
    };
    add_trace(first_index);
    add_trace(second_index);

    if (sol.profit > incumbent_solution.profit) {
        result.solution = std::move(sol);
    } else {
        result.solution = incumbent_solution.solution("faithful");
    }
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
