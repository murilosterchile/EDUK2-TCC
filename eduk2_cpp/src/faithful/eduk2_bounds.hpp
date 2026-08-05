#pragma once

#include "ukp/bounds.hpp"

namespace ukp::faithful::detail {

struct BoundPhase {
    BoundContext context;
    BoundValue global;
    Profit incumbent = 0;
    long long best_count = 0;
};

BoundPhase initialize_bounds(const std::vector<Item>& items, Weight capacity);

}  // namespace ukp::faithful::detail
