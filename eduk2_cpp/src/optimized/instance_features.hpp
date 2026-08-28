#pragma once

#include "ukp/types.hpp"

namespace ukp::optimized::detail {

InstanceFeatures extract_instance_features(
    const Instance& original, const std::vector<Item>& common_items);

}  // namespace ukp::optimized::detail
