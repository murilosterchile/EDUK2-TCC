#include "ukp/terminating_step_off.hpp"

#include "preprocessing.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace ukp::optimized {
namespace {

// Gilmore--Gomory step-off order: nondecreasing efficiency.  On an efficiency
// tie, the lighter item is later and is therefore the distinguished best item.
// The final ID key makes the predecessor/tie policy deterministic.
bool step_off_order(const Item& a, const Item& b) {
    const __int128 lhs = static_cast<__int128>(a.p) * b.w;
    const __int128 rhs = static_cast<__int128>(b.p) * a.w;
    if (lhs != rhs) return lhs < rhs;
    if (a.w != b.w) return a.w > b.w;
    return a.id > b.id;
}

bool checked_table_size(Weight capacity, std::size_t limit,
                        std::size_t& count, std::size_t& bytes) {
    if (capacity < 0) return false;
    const auto unsigned_capacity = static_cast<unsigned long long>(capacity);
    if (unsigned_capacity >= std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(unsigned_capacity) + 1;
    constexpr std::size_t bytes_per_state = sizeof(Profit) + sizeof(std::int32_t);
    if (count > std::numeric_limits<std::size_t>::max() / bytes_per_state) return false;
    bytes = count * bytes_per_state;
    return bytes <= limit && count <= std::vector<Profit>().max_size() &&
           count <= std::vector<std::int32_t>().max_size();
}

}  // namespace

TerminatingStepOff::TerminatingStepOff(TsoOptions options) : options_(options) {}

TsoResult TerminatingStepOff::solve(const Instance& instance) const {
    if (instance.capacity < 0) throw std::invalid_argument("negative capacity");

    TsoResult result;
    result.telemetry.original_items = static_cast<long long>(instance.items.size());
    result.telemetry.capacity = instance.capacity;
    result.solution.solver_name = "optimized_tso";
    result.solution.multiplicity_by_id.assign(instance.items.size(), 0);

    // This is intentionally the only preprocessing shared with EDUK2: it
    // merely removes nonpositive and individually infeasible items.
    std::vector<Item> items = detail::common_preprocess_items(instance);
    result.telemetry.after_common_preprocessing_items =
        static_cast<long long>(items.size());
    if (instance.capacity == 0 || items.empty()) {
        result.solution.optimal = true;
        result.status = TsoStatus::ProvedOptimal;
        result.status_message = "proved_optimal_empty";
        return result;
    }
    if (items.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() - 1)) {
        result.status_message = "kernel_not_applicable_too_many_items";
        return result;
    }

    std::size_t state_count = 0;
    std::size_t estimated_bytes = 0;
    if (!checked_table_size(instance.capacity, options_.max_dp_bytes,
                            state_count, estimated_bytes)) {
        result.status_message = "kernel_not_applicable_memory_budget";
        return result;
    }
    result.telemetry.estimated_dp_bytes = estimated_bytes;

    std::sort(items.begin(), items.end(), step_off_order);
    const std::int32_t n = static_cast<std::int32_t>(items.size());
    const std::int32_t copied = n;
    const Item& best = items.back();
    result.telemetry.best_item_weight = best.w;
    Weight gcd = 0;
    for (const Item& item : items) gcd = std::gcd(gcd, item.w);
    result.telemetry.weight_gcd = gcd;

    std::vector<Weight> suffix_max_weight(items.size());
    suffix_max_weight.back() = items.back().w;
    for (std::size_t i = items.size() - 1; i-- > 0;) {
        suffix_max_weight[i] = std::max(suffix_max_weight[i + 1], items[i].w);
    }

    std::vector<Profit> value;
    std::vector<std::int32_t> predecessor;
    try {
        value.assign(state_count, 0);
        predecessor.assign(state_count, copied);
    } catch (const std::bad_alloc&) {
        result.status_message = "kernel_not_applicable_allocation_failed";
        return result;
    } catch (const std::length_error&) {
        result.status_message = "kernel_not_applicable_allocation_failed";
        return result;
    }
    predecessor[0] = 0;

    Weight y = 0;
    Weight x_star = 0;
    Weight lambda = suffix_max_weight.front();
    while (true) {
        ++result.telemetry.states_scanned;
        const std::int32_t first = predecessor[static_cast<std::size_t>(y)];
        for (std::int32_t j = first; j < n; ++j) {
            const Item& item = items[static_cast<std::size_t>(j)];
            if (item.w > instance.capacity - y) continue;
            ++result.telemetry.transitions_considered;
            const Weight destination = y + item.w;
            const Profit candidate = safe_add(value[static_cast<std::size_t>(y)], item.p);
            Profit& current = value[static_cast<std::size_t>(destination)];
            std::int32_t& current_predecessor =
                predecessor[static_cast<std::size_t>(destination)];
            if (candidate > current) {
                current = candidate;
                current_predecessor = j;
                ++result.telemetry.transitions_improved;
            } else if (candidate == current && j > current_predecessor) {
                // The largest index leaves the smallest subsequent step-off
                // set and is the revisited algorithm's correctness-safe tie.
                current_predecessor = j;
                ++result.telemetry.ties_reassigned;
            }
        }

        const __int128 termination =
            static_cast<__int128>(x_star) + static_cast<__int128>(lambda);
        if (y >= instance.capacity || static_cast<__int128>(y) >= termination) break;
        ++y;
        const std::size_t yi = static_cast<std::size_t>(y);
        if (value[yi] > value[yi - 1]) {
            if (predecessor[yi] < n - 1) {
                x_star = y;
                lambda = suffix_max_weight[static_cast<std::size_t>(predecessor[yi])];
            }
        } else {
            value[yi] = value[yi - 1];
            predecessor[yi] = copied;
        }
    }

    result.telemetry.last_capacity_scanned = y;
    const __int128 termination =
        static_cast<__int128>(x_star) + static_cast<__int128>(lambda);
    result.telemetry.termination_level = termination > instance.capacity
        ? instance.capacity : static_cast<Weight>(termination);
    result.telemetry.terminated_early = y < instance.capacity;

    Weight reconstruction_weight = y;
    if (result.telemetry.terminated_early) {
        const long long best_count = (instance.capacity - y) / best.w + 1;
        result.solution.multiplicity_by_id[static_cast<std::size_t>(best.id)] = best_count;
        reconstruction_weight = instance.capacity - safe_mul(best_count, best.w);
    }
    while (reconstruction_weight > 0 &&
           predecessor[static_cast<std::size_t>(reconstruction_weight)] == copied) {
        --reconstruction_weight;
    }
    while (reconstruction_weight > 0) {
        const std::int32_t j = predecessor[static_cast<std::size_t>(reconstruction_weight)];
        if (j < 0 || j >= n) throw std::logic_error("invalid TSO predecessor");
        const Item& item = items[static_cast<std::size_t>(j)];
        ++result.solution.multiplicity_by_id[static_cast<std::size_t>(item.id)];
        reconstruction_weight -= item.w;
    }

    for (const Item& item : instance.items) {
        if (item.id < 0 || static_cast<std::size_t>(item.id) >=
                               result.solution.multiplicity_by_id.size()) {
            throw std::invalid_argument("item id is outside multiplicity vector");
        }
        const long long count =
            result.solution.multiplicity_by_id[static_cast<std::size_t>(item.id)];
        result.solution.weight = safe_add(result.solution.weight, safe_mul(count, item.w));
        result.solution.profit = safe_add(result.solution.profit, safe_mul(count, item.p));
    }
    if (result.solution.weight > instance.capacity) {
        throw std::logic_error("TSO reconstruction exceeds capacity");
    }
    result.solution.optimal = true;
    result.status = TsoStatus::ProvedOptimal;
    result.status_message = "proved_optimal";
    return result;
}

}  // namespace ukp::optimized
