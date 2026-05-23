#pragma once

#include "ukp/bounds.hpp"
#include "ukp/types.hpp"

namespace ukp::faithful {

class Solver {
public:
    explicit Solver(SolverOptions options = {});
    SolverResult solve(const Instance& inst);

private:
    SolverOptions options_;
};

}  // namespace ukp::faithful
