#include "instance_features.hpp"

#include "kernel_dispatcher.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace ukp::optimized::detail {
namespace {

bool within_dispatch_efficiency_band(const Item& candidate, const Item& best) {
    const __int128 candidate_cross =
        static_cast<__int128>(candidate.p) * best.w;
    const __int128 best_cross =
        static_cast<__int128>(best.p) * candidate.w;
    if (candidate_cross >= best_cross) return true;
    return best_cross - candidate_cross <=
           best_cross / kDispatcherEfficiencyBandDenominator;
}

bool survives_common_preprocessing(const Instance& instance, const Item& item) {
    if (item.w < 0) throw std::invalid_argument("negative item weight");
    if (item.w == 0) {
        if (item.p > 0) {
            throw std::invalid_argument(
                "unbounded UKP: positive-profit item has zero weight");
        }
        return false;
    }
    return item.p > 0 && item.w <= instance.capacity;
}

void publish_dispatch_shape(
    InstanceFeatures& features, const Item& best, const Item& worst) {
    features.best_item_id = best.id;
    features.best_item_weight = best.w;
    features.best_item_profit = best.p;
    features.best_item_efficiency = static_cast<double>(best.p) / best.w;
    features.capacity_over_best_weight =
        static_cast<double>(features.capacity) / best.w;
    features.capacity_over_min_weight =
        static_cast<double>(features.capacity) / features.min_weight;
    if (within_dispatch_efficiency_band(worst, best)) {
        features.near_best_efficiency_items =
            features.after_common_preprocessing_items;
    }
}

InstanceFeatures extract_dispatch_features_from_common(
    const Instance& original, const std::vector<Item>& common_items) {
    InstanceFeatures features;
    features.original_items = static_cast<long long>(original.items.size());
    features.capacity = original.capacity;
    features.after_common_preprocessing_items =
        static_cast<long long>(common_items.size());
    if (!original.items.empty()) {
        features.common_reduction_ratio =
            static_cast<double>(common_items.size()) / original.items.size();
    }
    if (common_items.empty() ||
        common_items.size() > static_cast<std::size_t>(kDispatcherMaxItems)) {
        return features;
    }

    const Item* best = &common_items.front();
    const Item* worst = &common_items.front();
    features.min_weight = features.max_weight = common_items.front().w;
    for (const Item& item : common_items) {
        features.min_weight = std::min(features.min_weight, item.w);
        features.max_weight = std::max(features.max_weight, item.w);
        if (better_ratio(item, *best)) best = &item;
        if (better_ratio(*worst, item)) worst = &item;
    }
    publish_dispatch_shape(features, *best, *worst);
    return features;
}

}  // namespace

InstanceFeatures extract_instance_features(
    const Instance& original, const std::vector<Item>& common_items) {
    InstanceFeatures f;
    f.original_items = static_cast<long long>(original.items.size());
    f.capacity = original.capacity;
    f.after_common_preprocessing_items = static_cast<long long>(common_items.size());
    if (!original.items.empty()) {
        f.common_reduction_ratio = static_cast<double>(common_items.size()) /
                                   static_cast<double>(original.items.size());
    }
    if (common_items.empty()) return f;
    f.min_weight = f.max_weight = common_items.front().w;
    f.min_profit = f.max_profit = common_items.front().p;
    const Item* best = &common_items.front();
    const Item* second = nullptr;
    long double weight_sum = 0.0L;
    for (const Item& item : common_items) {
        f.min_weight = std::min(f.min_weight, item.w);
        f.max_weight = std::max(f.max_weight, item.w);
        f.min_profit = std::min(f.min_profit, item.p);
        f.max_profit = std::max(f.max_profit, item.p);
        f.weight_gcd = std::gcd(f.weight_gcd, item.w);
        weight_sum += static_cast<long double>(item.w);
        if (better_ratio(item, *best)) {
            second = best;
            best = &item;
        } else if (&item != best &&
                   (second == nullptr || better_ratio(item, *second))) {
            second = &item;
        }
    }
    f.best_item_id = best->id;
    f.best_item_weight = best->w;
    f.best_item_profit = best->p;
    f.best_item_efficiency = static_cast<double>(best->p) / best->w;
    f.capacity_over_best_weight = static_cast<double>(original.capacity) / best->w;
    f.capacity_over_min_weight = static_cast<double>(original.capacity) / f.min_weight;
    f.mean_weight = static_cast<double>(weight_sum / common_items.size());
    long double squared_deviation_sum = 0.0L;
    const long double best_efficiency = static_cast<long double>(best->p) / best->w;
    for (const Item& item : common_items) {
        const long double delta = static_cast<long double>(item.w) - f.mean_weight;
        squared_deviation_sum += delta * delta;
        if (within_dispatch_efficiency_band(item, *best)) {
            ++f.near_best_efficiency_items;
        }
    }
    f.weight_variance = static_cast<double>(
        squared_deviation_sum / common_items.size());
    if (second != nullptr) {
        f.best_second_efficiency_gap = static_cast<double>(
            best_efficiency - static_cast<long double>(second->p) / second->w);
    }
    return f;
}

InstanceFeatures extract_dispatch_features(const Instance& original) {
    InstanceFeatures features;
    features.original_items = static_cast<long long>(original.items.size());
    features.capacity = original.capacity;

    // This conservative O(1) gate is what keeps the common EDUK2 path cheap on
    // large families.  It can only create a TSO false negative; the actual
    // EDUK2 preprocessing still validates every item afterwards.
    if (original.items.empty() ||
        original.items.size() > static_cast<std::size_t>(kDispatcherMaxItems)) {
        return features;
    }

    const Item* best = nullptr;
    const Item* worst = nullptr;
    long long feasible_count = 0;
    for (const Item& item : original.items) {
        if (!survives_common_preprocessing(original, item)) continue;
        ++feasible_count;
        if (best == nullptr) {
            best = worst = &item;
            features.min_weight = features.max_weight = item.w;
            continue;
        }
        features.min_weight = std::min(features.min_weight, item.w);
        features.max_weight = std::max(features.max_weight, item.w);
        if (better_ratio(item, *best)) best = &item;
        if (better_ratio(*worst, item)) worst = &item;
    }

    features.after_common_preprocessing_items = feasible_count;
    if (!original.items.empty()) {
        features.common_reduction_ratio =
            static_cast<double>(feasible_count) / original.items.size();
    }
    if (best == nullptr) return features;

    publish_dispatch_shape(features, *best, *worst);
    return features;
}

InstanceFeatures extract_dispatch_features(
    const Instance& original, const std::vector<Item>& common_items) {
    return extract_dispatch_features_from_common(original, common_items);
}

}  // namespace ukp::optimized::detail