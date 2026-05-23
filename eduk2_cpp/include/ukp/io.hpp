#pragma once

#include "ukp/types.hpp"
#include <iosfwd>
#include <string>

namespace ukp {

Instance read_instance(std::istream& in);
Instance read_instance_file(const std::string& path);
void write_solution(std::ostream& out, const Solution& sol);

}  // namespace ukp
