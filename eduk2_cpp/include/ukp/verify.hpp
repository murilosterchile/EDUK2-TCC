#pragma once

#include "ukp/types.hpp"

namespace ukp {

bool verify_solution(const Instance& inst, const Solution& sol);
Profit dense_dp_value(const Instance& inst);

}  // namespace ukp
