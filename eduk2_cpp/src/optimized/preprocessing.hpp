#pragma once

#include "eduk2_bounds.hpp"

namespace ukp::optimized::detail {

struct PreprocessResult {
    std::vector<Item> items;
    Item best_item;
    long long simple_removed = 0;
    long long multiple_removed = 0;
};

// Kernel-independent feasibility filtering shared by direct/forced TSO and
// full-telemetry paths.
std::vector<Item> common_preprocess_items(const Instance& instance);
std::vector<Item> tso_preprocess_items(
    std::vector<Item> common_items, bool use_multiple_dominance);

// Existing path for callers that already own a common-preprocessed vector.
PreprocessResult preprocess_items_for_eduk2(
    std::vector<Item>& common_items, bool use_simple_dominance);

// Fast AUTO/EDUK2 fallback path.  It constructs the EDUK2 working vector
// directly from Instance, avoiding a separate common_items allocation/copy on
// every instance that the dispatcher leaves on EDUK2.
PreprocessResult preprocess_items_for_eduk2(
    const Instance& instance, bool use_simple_dominance);

std::vector<Item> reduce_variables_by_bound(
    const std::vector<Item>& items,
    const BoundContext& context,
    Weight capacity,
    Profit incumbent,
    long long& bound_calls,
    BoundPolicy policy,
    BoundDecisionTelemetry* decision_telemetry = nullptr);

}  // namespace ukp::optimized::detail