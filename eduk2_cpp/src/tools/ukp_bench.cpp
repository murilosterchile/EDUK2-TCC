#include "ukp/faithful_solver.hpp"
#include "ukp/generator.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/verify.hpp"
#include <chrono>
#include <iostream>

using namespace ukp;

static long long ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

int main() {
    SolverOptions opt;
    std::cout << "family,n,c,solver,profit,verified,time_ms,states,items_after\n";
    for (int n : {50, 100, 200}) {
        Instance inst = make_strongly_correlated(n, 1000, 5, 50000 + n * 100);
        auto t0 = std::chrono::steady_clock::now();
        auto a = faithful::Solver(opt).solve(inst);
        auto ta = ms_since(t0);
        t0 = std::chrono::steady_clock::now();
        auto b = optimized::Solver(opt).solve(inst);
        auto tb = ms_since(t0);
        std::cout << "sc," << n << ',' << inst.capacity << ",faithful," << a.solution.profit << ','
                  << verify_solution(inst, a.solution) << ',' << ta << ',' << a.stats.states_scanned << ','
                  << a.stats.after_preprocess_items << '\n';
        std::cout << "sc," << n << ',' << inst.capacity << ",optimized," << b.solution.profit << ','
                  << verify_solution(inst, b.solution) << ',' << tb << ',' << b.stats.states_scanned << ','
                  << b.stats.after_preprocess_items << '\n';
    }
    return 0;
}
