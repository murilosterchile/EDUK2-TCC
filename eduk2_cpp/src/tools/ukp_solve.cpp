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
        std::cerr << "usage: ukp_solve <faithful|optimized> <instance-file> [--paper-faithful|--no-paper-faithful]"
                     " [--simple-dominance] [--core-remainder-ordering] [--modular-dominance]"
                     " [--core-multiple-dominance]\n";
        return 2;
    }
    std::string solver = argv[1];
    Instance inst = read_instance_file(argv[2]);
    SolverOptions opt;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--paper-faithful") opt.paper_faithful_mode = true;
        else if (arg == "--no-paper-faithful") opt.paper_faithful_mode = false;
        else if (arg == "--simple-dominance") opt.use_simple_dominance = true;
        else if (arg == "--core-remainder-ordering") opt.use_core_remainder_ordering = true;
        else if (arg == "--modular-dominance") opt.use_modular_dominance = true;
        else if (arg == "--core-multiple-dominance") opt.use_core_multiple_dominance = true;
        else { std::cerr << "unknown option: " << arg << '\n'; return 2; }
    }
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
    std::cout << "items_removed_core_multiple " << res.stats.items_removed_core_multiple << '\n';
    std::cout << "incumbent_improvements_bb " << res.stats.incumbent_improvements_bb << '\n';
    std::cout << "incumbent_improvements_dp " << res.stats.incumbent_improvements_dp << '\n';
    std::cout << "active_items_final " << res.stats.active_items_final << '\n';
    std::cout << "bound_winner " << res.stats.bound_winner << '\n';
    std::cout << "paper_faithful_mode " << (opt.paper_faithful_mode ? 1 : 0) << '\n';
    std::cout << "global_bound_used " << res.stats.global_bound_used << '\n';
    std::cout << "dp_stop_reason " << res.stats.dp_stop_reason << '\n';
    for (const auto& [type, calls] : res.stats.contextual_bound_calls) {
        std::cout << "contextual_bound_calls " << type << ' ' << calls << '\n';
    }
    for (const SliceStats& slice : res.stats.slices) {
        std::cout << "slice begin=" << slice.begin << " end=" << slice.end
                  << " states_entered=" << slice.states_entered
                  << " successor_attempts=" << slice.successor_attempts
                  << " states_created=" << slice.states_created
                  << " states_kept=" << slice.states_kept
                  << " states_fathomed_by_bound=" << slice.states_fathomed_by_bound
                  << " items_removed_threshold=" << slice.items_removed_threshold
                  << " active_items_before=" << slice.active_items_before
                  << " active_items_after=" << slice.active_items_after
                  << " contextual_bound_used=" << slice.contextual_bound_used << '\n';
    }
    std::cout << "periodicity_level " << res.stats.periodicity_level << '\n';
    std::cout << "stop_reason " << res.stats.stop_reason << '\n';
    return 0;
}
