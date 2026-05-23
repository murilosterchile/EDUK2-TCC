#pragma once

#include "ukp/types.hpp"
#include <vector>

namespace ukp {

std::vector<Item> remove_simple_dominated(std::vector<Item> items);
std::vector<Item> remove_multiple_dominated_by_best(std::vector<Item> items, const Item& best);
bool threshold_dominated_by_best(const Item& item, const Item& best, Weight capacity);

}  // namespace ukp
