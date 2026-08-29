#pragma once

#include "ukp/types.hpp"

namespace ukp::optimized::detail {

InstanceFeatures extract_instance_features(
    const Instance& original, const std::vector<Item>& common_items);

// Fast production path.  It scans the original item array directly and
// reproduces the subset that common_preprocess_items would expose to the
// dispatcher, but it does not allocate or materialize that subset.  Instances
// with more than kDispatcherMaxItems original items are rejected in O(1).
InstanceFeatures extract_dispatch_features(const Instance& original);

// Compatibility/testing overload for an already-materialized common vector.
InstanceFeatures extract_dispatch_features(
    const Instance& original, const std::vector<Item>& common_items);

}  // namespace ukp::optimized::detail