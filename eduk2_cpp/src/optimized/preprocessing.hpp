#pragma once

#include "eduk2_bounds.hpp"

namespace ukp::optimized::detail {

struct PreprocessResult {
    std::vector<Item> items;
    Item best_item;
    long long simple_removed = 0;
    long long multiple_removed = 0;
};

// UKP maximizes profit with nonnegative multiplicities. Negative weights are
// invalid, while a zero-weight positive-profit item makes the optimum
// unbounded and is rejected because the public API has no infinity result.
// Zero-weight nonpositive-profit, positive-weight nonpositive-profit, and
// individually capacity-infeasible items cannot improve a finite optimum and
// are discarded.
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
