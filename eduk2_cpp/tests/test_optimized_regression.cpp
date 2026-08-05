#include "ukp/io.hpp"
#include "ukp/generator.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/verify.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ukp;

namespace {

void check_against_dense(const Instance& instance, const char* name) {
    const SolverResult result = optimized::Solver().solve(instance);
    if (!verify_solution(instance, result.solution)) {
        throw std::runtime_error(std::string(name) + ": infeasible optimized solution");
    }
    if (result.solution.profit != dense_dp_value(instance)) {
        throw std::runtime_error(std::string(name) + ": optimized differs from dense oracle");
    }
}

}  // namespace

int main() {
    try {
        const auto path = std::filesystem::path(UKP_SOURCE_DIR) / "data" / "exnsdbis10.ukp";
        const Instance instance = read_instance_file(path.string());
        const SolverResult result = optimized::Solver().solve(instance);

        if (!verify_solution(instance, result.solution)) {
            throw std::runtime_error("optimized regression solution is infeasible");
        }
        // Reference value produced by PYAsUKP and independently reconstructed by
        // faithful.  The former threshold-only deletion returned 1,028,030.
        if (result.solution.profit != 1'028'035) {
            throw std::runtime_error("optimized regression profit differs from certified value");
        }
        for (unsigned seed = 1; seed <= 12; ++seed) {
            check_against_dense(make_random_instance(20, 1, 100, 500, seed), "random oracle");
        }
        check_against_dense(Instance{2900, {{0, 120, 300}, {1, 245, 580}, {2, 130, 301},
                                         {3, 260, 601}, {4, 310, 605}, {5, 194, 322},
                                         {6, 190, 310}}},
                            "article SAW oracle");
        std::cout << "optimized regression passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
