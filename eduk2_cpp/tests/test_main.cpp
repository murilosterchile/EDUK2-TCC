#include "ukp/faithful_solver.hpp"
#include "ukp/generator.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/verify.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace ukp;

static void require(bool cond, const char* msg) {
    if (!cond) throw std::runtime_error(msg);
}

static void check_instance(const Instance& inst) {
    SolverOptions opt;
    auto a = faithful::Solver(opt).solve(inst);
    auto b = optimized::Solver(opt).solve(inst);
    require(verify_solution(inst, a.solution), "faithful solution failed verification");
    require(verify_solution(inst, b.solution), "optimized solution failed verification");
    require(a.solution.profit == b.solution.profit, "faithful and optimized disagree");
    if (inst.capacity <= 20000) {
        Profit exact = dense_dp_value(inst);
        require(a.solution.profit == exact, "solution differs from dense exact DP");
    }
}

int main() {
    try {
        check_instance(Instance{10, {{0, 5, 10}, {1, 4, 7}, {2, 6, 12}}});
        check_instance(Instance{63, {{0, 15, 17}, {1, 20, 30}, {2, 25, 40}}});
        check_instance(Instance{2900, {{0, 119, 119}, {1, 120, 297}, {2, 131, 309}}});
        for (int seed = 1; seed <= 20; ++seed) {
            check_instance(make_random_instance(20, 1, 100, 500, static_cast<unsigned>(seed)));
        }
        check_instance(make_strongly_correlated(50, 101, 5, 5000));
        std::cout << "all tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "test failure: " << e.what() << '\n';
        return 1;
    }
}
