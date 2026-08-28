#include "instance_features.hpp"

#include <cmath>
#include <numeric>

namespace ukp::optimized::detail {

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
        } else if (&item != best && (second == nullptr || better_ratio(item, *second))) {
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
        const long double efficiency = static_cast<long double>(item.p) / item.w;
        if (efficiency >= best_efficiency * 0.99L) ++f.near_best_efficiency_items;
    }
    f.weight_variance = static_cast<double>(squared_deviation_sum / common_items.size());
    if (second != nullptr) {
        f.best_second_efficiency_gap = static_cast<double>(
            best_efficiency - static_cast<long double>(second->p) / second->w);
    }
    return f;
}

}  // namespace ukp::optimized::detail
