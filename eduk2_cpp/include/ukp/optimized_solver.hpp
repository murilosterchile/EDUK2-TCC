#pragma once

#include "ukp/types.hpp"

namespace ukp::optimized {

class Solver {
public:
    explicit Solver(SolverOptions options = {});
    SolverResult solve(const Instance& inst);

private:
    SolverOptions options_;
};

}  // namespace ukp::optimized
