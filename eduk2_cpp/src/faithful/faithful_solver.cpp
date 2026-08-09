#include "ukp/faithful_solver.hpp"
#include "ukp/dominance.hpp"
#include "eduk2_bounds.hpp"
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

void core_search(const std::vector<Item>& core, const BoundContext& bounds,
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
            compute_bound(bounds, capacity - next_weight).upper);
        if (upper > result.profit) {
            current[static_cast<std::size_t>(item.id)] = count;
            core_search(core, bounds, capacity, global_upper, node_limit, k + 1,
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
    core_search(items, bounds, capacity, global_upper, std::max<long long>(0, node_limit),
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
        const Profit residual_upper = compute_bound(search.bounds, search.capacity - next_weight).upper;
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

CoreSearchResult traverse_core(std::vector<Item> items, const BoundContext& bounds,
                               Weight capacity, Profit global_upper, int requested_core,
                               long long limit, std::size_t original_count,
                               const EffectiveOptions& effective) {
    const int default_size = std::min<int>(items.size(), std::max(100, static_cast<int>(items.size() / 100)));
    const int core_size = std::max(1, std::min(requested_core > 0 ? requested_core : default_size,
                                                static_cast<int>(items.size())));
    const auto core_order = [capacity, &effective](const Item& left, const Item& right) {
        if (!effective.core_remainder_ordering) return better_ratio(left, right);
        const Weight left_remainder = capacity % left.w;
        const Weight right_remainder = capacity % right.w;
        if (left_remainder != right_remainder) return left_remainder < right_remainder;
        return better_ratio(left, right);
    };
    std::sort(items.begin(), items.end(), core_order);
    items.resize(static_cast<std::size_t>(core_size));
    // Core-local multiple dominance before branching.
    std::vector<Item> filtered;
    long long core_multiple_removed = 0;
    long long core_modular_removed = 0;
    for (const Item& candidate : items) {
        bool dominated = false;
        bool modular = false;
        for (const Item& kept : filtered) {
            if (effective.core_multiple_dominance) {
                const long long copies = candidate.w / kept.w;
                if (copies > 0 && safe_mul(copies, kept.p) >= candidate.p) { dominated = true; break; }
            }
            // Modular dominance from dominance.ml/zhubrougan.  The best-ratio
            // item supplies the congruence adjustment; retained item `kept`
            // supplies the residue-class representative.
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
    CoreTraversal search{filtered, bounds, capacity, global_upper, std::max<long long>(0, limit), {}, {}};
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

[[maybe_unused]] std::vector<Item> reduce_variables_by_bound(const std::vector<Item>& items,
                                                              const BoundContext& context,
                                                              Weight capacity,
                                                              Profit incumbent,
                                                              long long& bound_calls) {
    std::vector<Item> reduced;
    reduced.reserve(items.size());
    for (const Item& item : items) {
        if (item.id == context.best.id) {
            reduced.push_back(item);
            continue;
        }
        const Weight remaining = capacity - item.w;
        if (remaining < 0) continue;
        const BoundValue bound = compute_bound(context, remaining);
        ++bound_calls;
        // MTU/EDUK2 variable reduction: if every solution containing item is
        // no better than z, its multiplicity can be fixed to zero.
        if (safe_add(item.p, bound.upper) > incumbent) reduced.push_back(item);
    }
    return reduced;
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
        const detail::BoundPhase bound_phase = detail::initialize_bounds(items, inst.capacity);
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
                                                  result.stats.bound_calls);
        result.stats.items_removed_bound = result.stats.after_preprocess_items - static_cast<long long>(items.size());
        std::sort(items.begin(), items.end(), better_ratio);
        ctx = make_bound_context(items);
        result.stats.after_preprocess_items = static_cast<long long>(items.size());
    }

    if (options_.use_core_bb) {
        CoreSearchResult core = traverse_core(items, ctx, inst.capacity,
                                               global_bound.upper, options_.core_size,
                                               options_.bb_node_limit, inst.items.size(), effective);
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
            result.stats.active_items_final = static_cast<long long>(items.size());
            result.stats.stop_reason = "core_bound_closed";
            return result;
        }
    }

    // EDUK's iteration space is represented by reachable exact-weight states,
    // not by an array indexed by every capacity.  Since all weights are
    // positive, map iteration is a topological order of the unbounded DP DAG.
    struct State {
        Weight weight;
        Profit profit;
        std::size_t predecessor;
        int item_id;
    };
    std::vector<State> states{{0, 0, 0, -1}};
    std::map<Weight, std::size_t> best_at_weight{{0, 0}};
    std::vector<unsigned char> active(items.size(), 1);
    std::vector<Weight> last_contribution(items.size(), 0);
    Profit envelope_profit = 0;
    Weight wmin = std::min_element(items.begin(), items.end(),
        [](const Item& a, const Item& b) { return a.w < b.w; })->w;
    Weight h = options_.slice_height > 0 ? options_.slice_height : wmin;
    if (h <= 0) h = 1;
    Weight wmax = 0;
    for (const Item& item : items) wmax = std::max(wmax, item.w);
    const Weight half_capacity = (inst.capacity + 1) / 2;
    // EDUK2 stops the forward DP around c/2, retaining one largest-item
    // interval so every feasible solution can be partitioned into two states
    // represented by this prefix.
    const Weight compute_limit = std::min(inst.capacity, half_capacity + wmax);
    bool closed_by_bound = false;

    auto consider_greedy_completion = [&](std::size_t state_index) {
        const State& state = states[state_index];
        std::vector<long long> multiplicity(inst.items.size(), 0);
        for (std::size_t cursor = state_index; cursor != 0; cursor = states[cursor].predecessor) {
            const int item_id = states[cursor].item_id;
            if (item_id < 0) break;
            ++multiplicity[static_cast<std::size_t>(item_id)];
        }

        Weight used_weight = state.weight;
        Profit candidate_profit = state.profit;
        for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
            if (!active[item_index]) continue;
            const Item& item = items[item_index];
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
    // The loop stops at the c/2 cut while retaining the extra wmax interval
    // required by the final two-state aggregation.
    for (Weight ya = 0; ya < half_capacity; ya += h) {
        const Weight yb = std::min(compute_limit, ya + h - 1);
        SliceStats slice;
        slice.begin = ya;
        slice.end = yb;
        for (unsigned char enabled : active) slice.active_items_before += enabled != 0;
        auto current = best_at_weight.lower_bound(ya);
        while (current != best_at_weight.end() && current->first <= yb) {
            const std::size_t state_index = current->second;
            const State state = states[state_index];
            ++current; // insertions have greater weight and must remain iterable.
            ++slice.states_entered;
            if (options_.use_bounds) {
                const BoundValue residual = compute_bound(ctx, inst.capacity - state.weight);
                ++result.stats.bound_calls;
                ++result.stats.contextual_bound_calls[bound_type_name(residual.type)];
                slice.contextual_bound_used = bound_type_name(residual.type);
                if (safe_add(state.profit, residual.upper) <= incumbent) {
                    ++result.stats.states_fathomed;
                    ++slice.states_fathomed_by_bound;
                    continue;
                }
            }
            // Listing 1's fathoming improves z by greedily completing every
            // surviving optimal state with the current dominance-free items.
            consider_greedy_completion(state_index);
            // `sequence_result` in PYAsUKP contains precisely the strict
            // envelope improvements.  Only those points update an item's last
            // contribution; states discarded by bounds do not.
            if (state.profit > envelope_profit) {
                envelope_profit = state.profit;
                if (state.item_id >= 0) {
                    const auto item_pos = std::find_if(items.begin(), items.end(),
                        [&](const Item& item) { return item.id == state.item_id; });
                    if (item_pos != items.end()) {
                        last_contribution[static_cast<std::size_t>(item_pos - items.begin())] = state.weight;
                    }
                }
            }
            ++result.stats.states_kept;
            ++slice.states_kept;
            for (std::size_t item_index = 0; item_index < items.size(); ++item_index) {
                if (!active[item_index]) continue;
                const Item& item = items[item_index];
                const Weight next_weight = state.weight + item.w;
                if (next_weight > compute_limit) continue;
                ++result.stats.states_scanned;
                ++slice.successor_attempts;
                const Profit next_profit = safe_add(state.profit, item.p);
                const auto known = best_at_weight.find(next_weight);
                if (known != best_at_weight.end() &&
                    states[known->second].profit >= next_profit) continue;
                states.push_back(State{next_weight, next_profit, state_index, item.id});
                best_at_weight[next_weight] = states.size() - 1;
                ++slice.states_created;
            }
        }
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
            continue;
        }
        std::size_t active_count = 0;
        for (unsigned char enabled : active) active_count += enabled != 0;
        for (std::size_t item_index = 0; item_index < items.size() && active_count > 1; ++item_index) {
            if (!active[item_index]) continue;
            if (safe_add(last_contribution[item_index], items[item_index].w) <= yb) {
                active[item_index] = 0;
                --active_count;
                ++result.stats.items_removed_threshold;
                ++slice.items_removed_threshold;
            }
        }
        slice.active_items_after = static_cast<long long>(active_count);
        result.stats.slices.push_back(std::move(slice));
        if (active_count == 1) break;
    }

    // c/2 cut: any feasible UKP solution can be split into two submultisets
    // whose weights are bounded by ceil(c/2)+wmax.  A prefix maximum lets us
    // select the best compatible partner in linear time over critical states.
    std::vector<std::pair<Weight, std::size_t>> prefix_best;
    prefix_best.reserve(best_at_weight.size());
    std::size_t running_best = 0;
    for (const auto& [weight, index] : best_at_weight) {
        if (states[index].profit > states[running_best].profit) running_best = index;
        prefix_best.emplace_back(weight, running_best);
    }
    std::size_t first_index = 0;
    std::size_t second_index = 0;
    Profit split_profit = 0;
    for (const auto& [weight, index] : best_at_weight) {
        const Weight remaining = inst.capacity - weight;
        auto pos = std::upper_bound(prefix_best.begin(), prefix_best.end(), remaining,
            [](Weight value, const auto& entry) { return value < entry.first; });
        if (pos == prefix_best.begin()) continue;
        --pos;
        const std::size_t partner = pos->second;
        const Profit candidate = safe_add(states[index].profit, states[partner].profit);
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

    auto add_trace = [&](std::size_t index) {
      while (index != 0) {
        const State& state = states[index];
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
    std::size_t active_count = 0;
    for (unsigned char enabled : active) active_count += enabled != 0;
    result.stats.active_items_final = static_cast<long long>(active_count);
    result.stats.dp_stop_reason = closed_by_bound ? "bound_closed" :
        (active_count == 1 ? "single_active_item" : "half_capacity_cut");
    result.stats.stop_reason = closed_by_bound ? "dp_bound_closed" :
        (active_count == 1 ? "single_item" : "half_capacity");
    return result;
}

}  // namespace ukp::faithful
