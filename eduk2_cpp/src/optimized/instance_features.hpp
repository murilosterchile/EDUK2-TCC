#pragma once

#include "ukp/types.hpp"

namespace ukp::optimized::detail {

InstanceFeatures extract_instance_features(
    const Instance& original, const std::vector<Item>& common_items);

// Computes only the fields needed to reject an instance cheaply. Full feature
// extraction is reserved for the small region where TSO can still be selected.
InstanceFeatures extract_dispatch_features(
    const Instance& original, const std::vector<Item>& common_items);

}  // namespace ukp::optimized::detail
