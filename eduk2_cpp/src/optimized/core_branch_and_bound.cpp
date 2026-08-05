#include "core_branch_and_bound.hpp"

#include "ukp/bounds.hpp"

#include <algorithm>
#include <limits>

namespace ukp::optimized::detail {
namespace {

struct SearchState {
    const std::vector<Item>* core = nullptr;
    Weight capacity = 0;
    long long node_limit = 0;
    long long nodes = 0;
    Profit best_profit = 0;
    Weight best_weight = 0;
    Profit target_upper = std::numeric_limits<Profit>::max();
    std::vector<long long> current;
    std::vector<long long> best;
    bool hit_limit = false;
    bool closed_gap = false;
};

Profit fractional_tail_bound(const std::vector<Item>& core, std::size_t k, Weight remaining) {
    if (remaining <= 0 || k >= core.size()) return 0;
    const Item& item = core[k];
    return floor_mul_div(remaining, item.p, item.w);
}

void search(SearchState& state, std::size_t k, Weight used_weight, Profit used_profit) {
    if (state.closed_gap) return;
    if (state.nodes >= state.node_limit) {
        state.hit_limit = true;
        return;
    }
    ++state.nodes;

    if (used_profit > state.best_profit ||
        (used_profit == state.best_profit && used_weight > state.best_weight)) {
        state.best_profit = used_profit;
        state.best_weight = used_weight;
        state.best = state.current;
        if (state.best_profit >= state.target_upper) {
            state.closed_gap = true;
            return;
        }
    }

    if (k >= state.core->size()) return;
    const auto& core = *state.core;
    const Item& item = core[k];
    const Weight remaining = state.capacity - used_weight;
    if (remaining <= 0) return;

    for (long long count = remaining / item.w; count >= 0; --count) {
        if (state.closed_gap) return;
        if (state.nodes >= state.node_limit) {
            state.hit_limit = true;
            return;
        }

        const Weight next_weight = used_weight + count * item.w;
        const Profit next_profit = safe_add(used_profit, safe_mul(count, item.p));
        Profit optimistic = next_profit;
        if (k + 1 < core.size()) {
            optimistic = safe_add(optimistic,
                                  fractional_tail_bound(core, k + 1, state.capacity - next_weight));
        }
        if (optimistic < state.best_profit) {
            if (count == 0) break;
            continue;
        }

        state.current[k] = count;
        if (next_profit > state.best_profit ||
            (next_profit == state.best_profit && next_weight > state.best_weight)) {
            state.best_profit = next_profit;
            state.best_weight = next_weight;
            state.best = state.current;
            if (state.best_profit >= state.target_upper) {
                state.closed_gap = true;
                state.current[k] = 0;
                return;
            }
        }
        if (k + 1 < core.size()) search(state, k + 1, next_weight, next_profit);
        state.current[k] = 0;
        if (count == 0) break;
    }
}

}  // namespace

CoreBBResult run_core_branch_and_bound(std::vector<Item> items,
                                       Weight capacity,
                                       long long node_limit,
                                       int requested_core_size,
                                       Profit incumbent,
                                       Profit global_upper,
                                       std::size_t original_item_count) {
    CoreBBResult result;
    result.multiplicity_by_id.assign(original_item_count, 0);
    if (items.empty() || node_limit <= 0) return result;

    std::sort(items.begin(), items.end(), better_ratio);
    const int requested = requested_core_size > 0 ? requested_core_size : 48;
    const int core_size = std::max(1, std::min<int>(requested, items.size()));
    items.resize(static_cast<std::size_t>(core_size));

    SearchState state;
    state.core = &items;
    state.capacity = capacity;
    state.node_limit = node_limit;
    state.best_profit = incumbent;
    state.target_upper = global_upper;
    state.current.assign(items.size(), 0);
    state.best.assign(items.size(), 0);

    const Item& best = items.front();
    const long long greedy_count = capacity / best.w;
    const Profit greedy_profit = safe_mul(greedy_count, best.p);
    if (greedy_profit > state.best_profit) {
        state.best_profit = greedy_profit;
        state.best_weight = greedy_count * best.w;
        state.best[0] = greedy_count;
    }
    if (state.best_profit >= state.target_upper) state.closed_gap = true;
    else search(state, 0, 0, 0);

    result.profit = state.best_profit;
    result.weight = state.best_weight;
    result.nodes = state.nodes;
    result.hit_limit = state.hit_limit;
    result.closed_gap = state.closed_gap || state.best_profit >= global_upper;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].id >= 0 && static_cast<std::size_t>(items[i].id) < result.multiplicity_by_id.size()) {
            result.multiplicity_by_id[static_cast<std::size_t>(items[i].id)] += state.best[i];
        }
    }
    return result;
}

AdaptiveBBController::AdaptiveBBController(long long configured_limit)
    : probe_nodes(std::min<long long>(std::max<long long>(0, configured_limit), 128)),
      max_nodes(std::max<long long>(0, configured_limit)) {}

bool AdaptiveBBController::should_escalate(const CoreBBResult& probe,
                                           Profit old_incumbent,
                                           Profit global_upper,
                                           long long items_after_preprocess) const {
    if (!enabled || max_nodes <= probe_nodes || probe.closed_gap) return false;
    const bool improved = probe.profit > old_incumbent;
    const Profit gap = std::max<Profit>(0, global_upper - probe.profit);
    const long double relative_gap = global_upper > 0
        ? static_cast<long double>(gap) / static_cast<long double>(global_upper)
        : 1.0L;
    return (improved && relative_gap <= 0.0005L) ||
           (items_after_preprocess <= 256 && relative_gap <= 0.002L);
}

}  // namespace ukp::optimized::detail
