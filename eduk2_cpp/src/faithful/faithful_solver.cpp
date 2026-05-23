#include "ukp/faithful_solver.hpp"
#include "ukp/dominance.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
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

Profit run_core_bb(std::vector<Item> items, Weight c, int requested_core,
                   long long node_limit, long long& nodes) {
    std::sort(items.begin(), items.end(), better_ratio);
    int core_size = requested_core > 0 ? requested_core : std::min<int>(static_cast<int>(items.size()), 100);
    core_size = std::max(1, std::min<int>(core_size, static_cast<int>(items.size())));
    items.resize(static_cast<size_t>(core_size));
    nodes = 0;
    return run_core_bb_rec(items, c, 0, 0, 0, 0, nodes, node_limit);
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
    result.stats.original_items = static_cast<long long>(inst.items.size());
    if (inst.items.empty() || inst.capacity == 0) {
        result.solution.multiplicity_by_id.assign(inst.items.size(), 0);
        result.solution.optimal = true;
        result.solution.solver_name = "faithful";
        return result;
    }

    std::vector<Item> items = inst.items;
    if (options_.use_preprocessing) {
        items = remove_simple_dominated(items);
        Item best = *std::max_element(items.begin(), items.end(),
            [](const Item& a, const Item& b) { return better_ratio(b, a); });
        items = remove_multiple_dominated_by_best(items, best);
    }
    std::sort(items.begin(), items.end(), better_ratio);
    result.stats.after_preprocess_items = static_cast<long long>(items.size());

    BoundContext ctx = make_bound_context(items);
    BoundValue global_bound = compute_bound(ctx, inst.capacity);
    result.stats.bound_calls++;

    const long long best_count = inst.capacity / ctx.best.w;
    Profit incumbent = safe_mul(best_count, ctx.best.p);

    // EDUK2 bound-stop shortcut.  This is exact only when the certified incumbent
    // is the one reconstructed here, so it is applied before the core B&B changes
    // the incumbent without storing its multiplicities.
    if (options_.use_bounds && incumbent >= global_bound.upper) {
        result.solution = solution_from_best_item(inst, ctx.best, best_count);
        return result;
    }

    if (options_.use_core_bb) {
        long long nodes = 0;
        incumbent = std::max(incumbent, run_core_bb(items, inst.capacity, options_.core_size,
                                                    options_.bb_node_limit, nodes));
        result.stats.bb_nodes += nodes;
    }

    const size_t cap = static_cast<size_t>(inst.capacity);
    std::vector<Cell> dp(cap + 1);
    Weight wmin = std::min_element(items.begin(), items.end(),
        [](const Item& a, const Item& b) { return a.w < b.w; })->w;
    Weight h = options_.slice_height > 0 ? options_.slice_height : wmin;
    if (h <= 0) h = 1;

    // Slice-oriented dynamic programming. This keeps the visible structure of EDUK:
    // capacities are processed in intervals, and bounds are evaluated at slice states.
    for (Weight ya = 0; ya < inst.capacity; ya += h) {
        Weight yb = std::min(inst.capacity, ya + h);
        for (Weight y = ya + 1; y <= yb; ++y) {
            Cell best = dp[static_cast<size_t>(y - 1)];
            best.prev_capacity = y - 1;
            best.last_item = -1;
            for (const Item& it : items) {
                if (it.w > y) continue;
                const Cell& prev = dp[static_cast<size_t>(y - it.w)];
                Profit candidate = safe_add(prev.profit, it.p);
                if (candidate > best.profit) {
                    best.profit = candidate;
                    best.last_item = it.id;
                    best.prev_capacity = y - it.w;
                }
            }
            dp[static_cast<size_t>(y)] = best;
            result.stats.states_scanned += static_cast<long long>(items.size());

            if (options_.use_bounds) {
                BoundValue b = compute_bound(ctx, inst.capacity - y);
                result.stats.bound_calls++;
                if (safe_add(best.profit, b.upper) <= incumbent) {
                    result.stats.states_fathomed++;
                } else {
                    result.stats.states_kept++;
                }
                incumbent = std::max(incumbent, safe_add(best.profit, b.lower));
            }
        }
    }

    Solution sol;
    sol.profit = dp[cap].profit;
    sol.weight = 0;
    sol.optimal = true;
    sol.solver_name = "faithful";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);

    Weight y = inst.capacity;
    while (y > 0) {
        const Cell& cell = dp[static_cast<size_t>(y)];
        if (cell.last_item < 0) {
            if (cell.prev_capacity >= y) break;
            y = cell.prev_capacity;
            continue;
        }
        const auto it = std::find_if(inst.items.begin(), inst.items.end(),
            [&](const Item& x) { return x.id == cell.last_item; });
        if (it == inst.items.end()) throw std::runtime_error("backtracking failed");
        sol.multiplicity_by_id[static_cast<size_t>(it->id)]++;
        sol.weight += it->w;
        y = cell.prev_capacity;
    }

    result.solution = std::move(sol);
    return result;
}

}  // namespace ukp::faithful
