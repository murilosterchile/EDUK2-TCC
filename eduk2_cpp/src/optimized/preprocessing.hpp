#pragma once

#include "eduk2_bounds.hpp"

namespace ukp::optimized::detail {

struct PreprocessResult {
    std::vector<Item> items;
    Item best_item;
    long long simple_removed = 0;
    long long multiple_removed = 0;
};

std::vector<Item> common_preprocess_items(const Instance& instance);
PreprocessResult preprocess_items_for_eduk2(
    const std::vector<Item>& common_items, bool use_simple_dominance);
std::vector<Item> reduce_variables_by_bound(
    const std::vector<Item>& items,
    const BoundContext& context,
    Weight capacity,
    Profit incumbent,
    long long& bound_calls,
    BoundPolicy policy,
    BoundDecisionTelemetry* decision_telemetry = nullptr);

}  // namespace ukp::optimized::detail
