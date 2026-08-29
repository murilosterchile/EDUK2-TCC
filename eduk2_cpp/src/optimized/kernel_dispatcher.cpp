#include "kernel_dispatcher.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ukp::optimized::detail {
namespace {

bool estimate_tso_memory(Weight capacity, std::size_t limit, std::size_t& bytes) {
    if (capacity < 0) return false;
    const auto unsigned_capacity = static_cast<unsigned long long>(capacity);
    if (unsigned_capacity >= std::numeric_limits<std::size_t>::max()) return false;
    const std::size_t states = static_cast<std::size_t>(unsigned_capacity) + 1;
    constexpr std::size_t bytes_per_state = sizeof(Profit) + sizeof(std::int32_t);
    if (states > std::numeric_limits<std::size_t>::max() / bytes_per_state) return false;
    bytes = states * bytes_per_state;
    return bytes <= limit;
}

bool estimated_work_within_limit(const InstanceFeatures& features) {
    const __int128 capacity = features.capacity;
    const __int128 min_weight = features.min_weight;
    const __int128 item_count = features.after_common_preprocessing_items;
    const __int128 limit = kDispatcherMaxEstimatedWork;

    // Exact comparison for C * min(n, C / w_min + 1) <= limit. The second
    // branch compares the rational expression by cross multiplication.
    if (item_count * min_weight <= capacity + min_weight) {
        return capacity * item_count <= limit;
    }
    return capacity * (capacity + min_weight) <= limit * min_weight;
}

}  // namespace

DispatchDecision dispatch_kernel(
    const InstanceFeatures& features, const TsoOptions& tso_options) {
    DispatchDecision decision;
    if (!estimate_tso_memory(features.capacity, tso_options.max_dp_bytes,
                             decision.estimated_tso_bytes)) {
        decision.reason = "tso_memory_unsafe";
        return decision;
    }
    if (features.after_common_preprocessing_items <= 0 || features.min_weight <= 0) {
        decision.reason = "no_dispatchable_items";
        return decision;
    }

    const long double reachable_item_steps = std::min<long double>(
        features.after_common_preprocessing_items,
        features.capacity_over_min_weight + 1.0L);
    decision.estimated_tso_work =
        static_cast<long double>(features.capacity) * reachable_item_steps;

    // Conservative region learned from family-separated benchmark data.  A
    // wrong choice cannot affect correctness, but TSO false positives are far
    // more expensive, so every condition must hold and EDUK2 is the default.
    const bool uniformly_near_best =
        features.near_best_efficiency_items ==
        features.after_common_preprocessing_items;
    if (features.after_common_preprocessing_items <= kDispatcherMaxItems &&
        uniformly_near_best &&
        static_cast<__int128>(features.capacity) <=
            static_cast<__int128>(kDispatcherMaxCapacityOverMinWeight) *
                features.min_weight &&
        estimated_work_within_limit(features)) {
        decision.kernel = KernelChoice::Tso;
        decision.reason = "small_uniform_efficiency_region";
    }
    return decision;
}

}  // namespace ukp::optimized::detail
