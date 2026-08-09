#pragma once

#include "ukp/bounds.hpp"

namespace ukp::faithful::detail {

struct PreprocessResult {
    std::vector<Item> items;
    long long simple_removed = 0;
    long long multiple_removed = 0;
};

PreprocessResult preprocess_items(const Instance& instance, bool use_simple_dominance);
std::vector<Item> reduce_variables_by_bound(const std::vector<Item>& items,
                                            const BoundContext& context,
                                            Weight capacity, Profit incumbent,
                                            long long& bound_calls, BoundPolicy policy);

}  // namespace ukp::faithful::detail
