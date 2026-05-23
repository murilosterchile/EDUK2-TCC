#pragma once

#include "ukp/types.hpp"
#include <cstdint>

namespace ukp {

Instance make_strongly_correlated(int n, Weight wmin, Profit alpha, Weight capacity);
Instance make_saw_like(int n, Weight wmin, Weight wmax, Weight capacity, std::uint64_t seed);
Instance make_random_instance(int n, Weight wmin, Weight wmax, Weight capacity, std::uint64_t seed);

}  // namespace ukp
