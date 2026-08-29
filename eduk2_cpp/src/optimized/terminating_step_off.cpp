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
    if (options_.max_transitions < 0) {
        throw std::invalid_argument("negative TSO transition budget");
    }

    return solve_with_common_items(
        instance, detail::common_preprocess_items(instance));
}

TsoResult TerminatingStepOff::solve_with_common_items(
    const Instance& instance, std::vector<Item> items) const {
    if (instance.capacity < 0) throw std::invalid_argument("negative capacity");
    if (options_.max_transitions < 0) {
        throw std::invalid_argument("negative TSO transition budget");
    }

    TsoResult result;
    result.telemetry.original_items = static_cast<long long>(instance.items.size());
    result.telemetry.capacity = instance.capacity;
    result.telemetry.original_capacity = instance.capacity;
    result.solution.solver_name = "optimized_tso";
    result.solution.multiplicity_by_id.assign(instance.items.size(), 0);

    // This is intentionally the only preprocessing shared with EDUK2: it
    // merely removes nonpositive and individually infeasible items.
    result.telemetry.after_common_preprocessing_items =
        static_cast<long long>(items.size());
    items = detail::tso_preprocess_items(
        std::move(items), options_.use_multiple_dominance);
    result.telemetry.after_tso_preprocessing_items =
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

    std::sort(items.begin(), items.end(), step_off_order);
    result.telemetry.best_item_weight = items.back().w;
    Weight gcd = 0;
    for (const Item& item : items) gcd = std::gcd(gcd, item.w);
    result.telemetry.weight_gcd = gcd;
    const Weight scale = options_.use_gcd_scaling && gcd > 1 ? gcd : 1;
    const Weight capacity = instance.capacity / scale;
    result.telemetry.gcd_scale_factor = scale;
    result.telemetry.scaled_capacity = capacity;

    std::size_t ignored_count = 0;
    std::size_t before_bytes = 0;
    checked_table_size(instance.capacity, std::numeric_limits<std::size_t>::max(),
                       ignored_count, before_bytes);
    result.telemetry.estimated_dp_bytes_before_scaling = before_bytes;

    if (scale > 1) {
        for (Item& item : items) item.w /= scale;
    }

    std::size_t state_count = 0;
    std::size_t estimated_bytes = 0;
    if (!checked_table_size(capacity, options_.max_dp_bytes,
                            state_count, estimated_bytes)) {
        result.telemetry.estimated_dp_bytes_after_scaling = estimated_bytes;
        result.status_message = "kernel_not_applicable_memory_budget";
        return result;
    }
    result.telemetry.estimated_dp_bytes = estimated_bytes;
    result.telemetry.estimated_dp_bytes_after_scaling = estimated_bytes;

    const std::int32_t n = static_cast<std::int32_t>(items.size());
    const std::int32_t copied = n;
    const Item& best = items.back();

    std::vector<Weight> suffix_max_weight(items.size());
    suffix_max_weight.back() = items.back().w;
    for (std::size_t i = items.size() - 1; i-- > 0;) {
        suffix_max_weight[i] = std::max(suffix_max_weight[i + 1], items[i].w);
    }

    std::vector<Profit> value;
    std::vector<std::int32_t> predecessor;
    const bool bounded_speculation = options_.max_transitions > 0;
    try {
        if (!bounded_speculation) {
            // Preserve the historical forced/unlimited TSO path exactly: one
            // full allocation and no growth checks on transition writes.
            value.assign(state_count, 0);
            predecessor.assign(state_count, copied);
        } else {
            // A bounded AUTO attempt should not zero C+1 states before it has
            // executed its first transition. Reserve the already memory-checked
            // address space, but construct only the range needed by the first
            // step-off wave. Growth below is geometric and preserves the same
            // zero/copy initialization semantics as the full table.
            value.reserve(state_count);
            predecessor.reserve(state_count);
            const std::size_t initial_states = std::min(
                state_count,
                static_cast<std::size_t>(suffix_max_weight.front()) + 1);
            value.resize(std::max<std::size_t>(initial_states, 1), 0);
            predecessor.resize(std::max<std::size_t>(initial_states, 1), copied);
        }
    } catch (const std::bad_alloc&) {
        result.status_message = "kernel_not_applicable_allocation_failed";
        return result;
    } catch (const std::length_error&) {
        result.status_message = "kernel_not_applicable_allocation_failed";
        return result;
    }
    predecessor[0] = 0;

    const auto ensure_bounded_state = [&](std::size_t index) -> bool {
        if (!bounded_speculation || index < value.size()) return true;
        std::size_t target = value.size();
        if (target == 0) target = 1;
        while (target <= index) {
            const std::size_t doubled =
                target > state_count / 2 ? state_count : target * 2;
            if (doubled <= target) {
                target = state_count;
                break;
            }
            target = doubled;
        }
        if (target <= index) return false;
        try {
            value.resize(target, 0);
            predecessor.resize(target, copied);
        } catch (const std::bad_alloc&) {
            return false;
        } catch (const std::length_error&) {
            return false;
        }
        return true;
    };

    Weight y = 0;
    Weight x_star = 0;
    Weight lambda = suffix_max_weight.front();
    while (true) {
        if (bounded_speculation &&
            !ensure_bounded_state(static_cast<std::size_t>(y))) {
            result.status_message = "kernel_not_applicable_allocation_failed";
            return result;
        }
        ++result.telemetry.states_scanned;
        const std::int32_t first = predecessor[static_cast<std::size_t>(y)];
        for (std::int32_t j = first; j < n; ++j) {
            const Item& item = items[static_cast<std::size_t>(j)];
            if (item.w > capacity - y) continue;
            // Check before starting the next transition. This admits a proof
            // that uses exactly max_transitions, but never executes transition
            // max_transitions + 1. The counter is hardware-independent.
            if (options_.max_transitions > 0 &&
                result.telemetry.transitions_considered >= options_.max_transitions) {
                result.status = TsoStatus::WorkBudgetExceeded;
                result.status_message = "work_budget_exceeded";
                result.solution.optimal = false;
                return result;
            }
            ++result.telemetry.transitions_considered;
            const Weight destination = y + item.w;
            if (bounded_speculation &&
                !ensure_bounded_state(static_cast<std::size_t>(destination))) {
                result.status_message = "kernel_not_applicable_allocation_failed";
                return result;
            }
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
        if (y >= capacity || static_cast<__int128>(y) >= termination) break;
        ++y;
        if (bounded_speculation &&
            !ensure_bounded_state(static_cast<std::size_t>(y))) {
            result.status_message = "kernel_not_applicable_allocation_failed";
            return result;
        }
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
    result.telemetry.termination_level = termination > capacity
        ? capacity : static_cast<Weight>(termination);
    result.telemetry.terminated_early = y < capacity;

    Weight reconstruction_weight = y;
    if (result.telemetry.terminated_early) {
        const long long best_count = (capacity - y) / best.w + 1;
        result.solution.multiplicity_by_id[static_cast<std::size_t>(best.id)] = best_count;
        reconstruction_weight = capacity - safe_mul(best_count, best.w);
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