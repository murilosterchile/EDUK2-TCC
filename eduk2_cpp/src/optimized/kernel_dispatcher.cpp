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

long double estimated_tso_work(const InstanceFeatures& features) {
    if (features.min_weight <= 0 ||
        features.after_common_preprocessing_items <= 0) {
        return 0.0L;
    }
    const long double reachable_item_steps = std::min<long double>(
        features.after_common_preprocessing_items,
        static_cast<long double>(features.capacity / features.min_weight) + 1.0L);
    return static_cast<long double>(features.capacity) * reachable_item_steps;
}

bool estimated_work_within_budget(
    const InstanceFeatures& features, long long max_transitions) {
    if (max_transitions == 0) return true;
    if (max_transitions < kDispatcherMinFiniteTransitionBudget) return false;
    if (features.min_weight <= 0 ||
        features.after_common_preprocessing_items <= 0) {
        return false;
    }

    // Exact comparison of C*min(n, floor(C/w_min)+1) <= budget.
    const __int128 capacity = features.capacity;
    const __int128 item_count = features.after_common_preprocessing_items;
    const __int128 min_weight = features.min_weight;
    const __int128 reachable = std::min<__int128>(
        item_count, capacity / min_weight + 1);
    return capacity * reachable <= static_cast<__int128>(max_transitions);
}

bool weight_span_is_narrow(const InstanceFeatures& features) {
    if (features.min_weight <= 0 || features.max_weight < features.min_weight) {
        return false;
    }
    return static_cast<__int128>(features.max_weight) *
               kDispatcherWeightSpanDenominator <=
           static_cast<__int128>(features.min_weight) *
               kDispatcherWeightSpanNumerator;
}

BestWeightOrientation best_weight_orientation(const InstanceFeatures& features) {
    const Weight best = features.best_item_weight;
    const Weight min_weight = features.min_weight;
    const Weight max_weight = features.max_weight;
    if (best <= 0 || min_weight <= 0 || max_weight < min_weight) {
        return BestWeightOrientation::Unknown;
    }

    const bool at_light_end =
        static_cast<__int128>(best) <=
        static_cast<__int128>(min_weight) +
            static_cast<__int128>(min_weight) /
                kDispatcherEndpointSlackDenominator;
    const bool at_heavy_end =
        static_cast<__int128>(best) >=
        static_cast<__int128>(max_weight) -
            static_cast<__int128>(max_weight) /
                kDispatcherEndpointSlackDenominator;

    if (!at_light_end && !at_heavy_end) return BestWeightOrientation::Unknown;
    if (at_light_end && at_heavy_end) {
        return best - min_weight <= max_weight - best
            ? BestWeightOrientation::Light
            : BestWeightOrientation::Heavy;
    }
    return at_light_end ? BestWeightOrientation::Light
                        : BestWeightOrientation::Heavy;
}

long double residual_pressure(
    const InstanceFeatures& features, BestWeightOrientation orientation) {
    if (features.capacity < 0 || features.best_item_weight <= 0 ||
        orientation == BestWeightOrientation::Unknown) {
        return 0.0L;
    }
    const Weight remainder = features.capacity % features.best_item_weight;
    const Weight numerator = orientation == BestWeightOrientation::Light
        ? remainder
        : features.best_item_weight - remainder;
    return static_cast<long double>(numerator) /
           static_cast<long double>(features.best_item_weight);
}

bool residual_pressure_is_high(
    const InstanceFeatures& features, BestWeightOrientation orientation) {
    if (features.capacity < 0 || features.best_item_weight <= 0 ||
        orientation == BestWeightOrientation::Unknown) {
        return false;
    }
    const __int128 best = features.best_item_weight;
    const __int128 remainder = features.capacity % features.best_item_weight;
    const __int128 numerator = orientation == BestWeightOrientation::Light
        ? remainder
        : best - remainder;
    return numerator * kDispatcherResidualPressureDenominator >=
           best * kDispatcherResidualPressureNumerator;
}

}  // namespace

DispatchDecision dispatch_kernel(
    const InstanceFeatures& features, const TsoOptions& tso_options) {
    DispatchDecision decision;
    decision.estimated_tso_work = estimated_tso_work(features);

    if (features.after_common_preprocessing_items <= 0 ||
        features.min_weight <= 0 || features.best_item_weight <= 0) {
        decision.reason = "cheap_structural_reject";
        return decision;
    }

    if (features.after_common_preprocessing_items > kDispatcherMaxItems) {
        decision.reason = "item_count_reject";
        return decision;
    }

    if (features.near_best_efficiency_items !=
        features.after_common_preprocessing_items) {
        decision.reason = "efficiency_band_reject";
        return decision;
    }

    if (!weight_span_is_narrow(features)) {
        decision.reason = "weight_span_reject";
        return decision;
    }

    decision.orientation = best_weight_orientation(features);
    switch (decision.orientation) {
        case BestWeightOrientation::Light:
            decision.orientation_name = "light";
            break;
        case BestWeightOrientation::Heavy:
            decision.orientation_name = "heavy";
            break;
        case BestWeightOrientation::Unknown:
            decision.orientation_name = "unknown";
            break;
    }
    if (decision.orientation == BestWeightOrientation::Unknown) {
        decision.reason = "best_weight_not_at_endpoint";
        return decision;
    }

    decision.residual_pressure = residual_pressure(features, decision.orientation);
    if (!residual_pressure_is_high(features, decision.orientation)) {
        decision.reason = "low_residual_pressure";
        return decision;
    }

    // A finite cap is only used as protection after the structural classifier
    // has selected a high-confidence TSO candidate.  Unlike v2, no 32x
    // discount is applied: the old factor suppressed practically every useful
    // candidate.  Budgets below 650M deliberately disable AUTO speculation.
    if (!estimated_work_within_budget(features, tso_options.max_transitions)) {
        decision.reason = "tso_budget_too_small_for_auto";
        return decision;
    }

    if (!estimate_tso_memory(features.capacity, tso_options.max_dp_bytes,
                             decision.estimated_tso_bytes)) {
        decision.reason = "tso_memory_unsafe";
        return decision;
    }

    decision.kernel = KernelChoice::Tso;
    decision.reason = "high_residual_pressure_near_proportional";
    return decision;
}

}  // namespace ukp::optimized::detail