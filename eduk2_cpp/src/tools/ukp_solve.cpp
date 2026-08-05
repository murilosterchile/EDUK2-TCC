#include "ukp/faithful_solver.hpp"
#include "ukp/io.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/verify.hpp"
#include <chrono>
#include <iostream>
#include <string>

using namespace ukp;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: ukp_solve <faithful|optimized> <instance-file>\n";
        return 2;
    }
    std::string solver = argv[1];
    Instance inst = read_instance_file(argv[2]);
    SolverOptions opt;
    auto t0 = std::chrono::steady_clock::now();
    SolverResult res;
    if (solver == "faithful") {
        res = faithful::Solver(opt).solve(inst);
    } else if (solver == "optimized") {
        res = optimized::Solver(opt).solve(inst);
    } else {
        std::cerr << "unknown solver: " << solver << "\n";
        return 2;
    }
    auto t1 = std::chrono::steady_clock::now();
    write_solution(std::cout, res.solution);
    std::cout << "verified " << (verify_solution(inst, res.solution) ? 1 : 0) << '\n';
    std::cout << "time_ms " << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << '\n';
    std::cout << "items_original " << res.stats.original_items << '\n';
    std::cout << "items_after_preprocess " << res.stats.after_preprocess_items << '\n';
    std::cout << "states_scanned " << res.stats.states_scanned << '\n';
    std::cout << "states_fathomed " << res.stats.states_fathomed << '\n';
    std::cout << "bound_calls " << res.stats.bound_calls << '\n';
    std::cout << "bb_nodes " << res.stats.bb_nodes << '\n';
    std::cout << "points_generated " << res.stats.points_generated << '\n';
    std::cout << "points_kept " << res.stats.states_kept << '\n';
    std::cout << "state_bytes_approx " << res.stats.estimated_state_bytes << '\n';
    std::cout << "items_removed_bound " << res.stats.items_removed_bound << '\n';
    std::cout << "items_removed_threshold " << res.stats.items_removed_threshold << '\n';
    std::cout << "items_removed_simple " << res.stats.items_removed_simple << '\n';
    std::cout << "items_removed_multiple " << res.stats.items_removed_multiple << '\n';
    std::cout << "items_removed_modular " << res.stats.items_removed_modular << '\n';
    std::cout << "incumbent_improvements_bb " << res.stats.incumbent_improvements_bb << '\n';
    std::cout << "incumbent_improvements_dp " << res.stats.incumbent_improvements_dp << '\n';
    std::cout << "active_items_final " << res.stats.active_items_final << '\n';
    std::cout << "bound_winner " << res.stats.bound_winner << '\n';
    std::cout << "periodicity_level " << res.stats.periodicity_level << '\n';
    std::cout << "stop_reason " << res.stats.stop_reason << '\n';
    return 0;
}
