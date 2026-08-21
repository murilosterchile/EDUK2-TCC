#include "ukp/faithful_solver.hpp"
#include "ukp/io.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/verify.hpp"
#include <chrono>
#include <array>
#include <iomanip>
#include <iostream>
#include <string>

using namespace ukp;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: ukp_solve <faithful|optimized> <instance-file> [--paper-faithful|--no-paper-faithful]"
                     " [--simple-dominance] [--core-remainder-ordering] [--modular-dominance]"
                     " [--core-multiple-dominance] [--bound-policy=u3|v|tau-star|best-item-star|best-certified]"
                     " [--verbose]\n";
        return 2;
    }
    std::string solver = argv[1];
    Instance inst = read_instance_file(argv[2]);
    SolverOptions opt;
    bool verbose = false;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--paper-faithful") opt.paper_faithful_mode = true;
        else if (arg == "--no-paper-faithful") opt.paper_faithful_mode = false;
        else if (arg == "--simple-dominance") opt.use_simple_dominance = true;
        else if (arg == "--core-remainder-ordering") opt.use_core_remainder_ordering = true;
        else if (arg == "--modular-dominance") opt.use_modular_dominance = true;
        else if (arg == "--core-multiple-dominance") opt.use_core_multiple_dominance = true;
        else if (arg == "--verbose") verbose = true;
        else if (arg.rfind("--bound-policy=", 0) == 0) {
            const std::string value = arg.substr(15);
            if (value == "u3") opt.bound_policy = BoundPolicy::U3;
            else if (value == "v") opt.bound_policy = BoundPolicy::V;
            else if (value == "tau-star") opt.bound_policy = BoundPolicy::TauStar;
            else if (value == "best-item-star") opt.bound_policy = BoundPolicy::BestItemStar;
            else if (value == "best-certified") opt.bound_policy = BoundPolicy::BestCertified;
            else { std::cerr << "unknown bound policy: " << value << '\n'; return 2; }
        }
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
    std::cout << "time_us " << std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() << '\n';
    std::cout << "items_original " << res.stats.original_items << '\n';
    std::cout << "items_after_preprocess " << res.stats.after_preprocess_items << '\n';
    std::cout << "states_scanned " << res.stats.states_scanned << '\n';
    std::cout << "states_expanded " << res.stats.states_expanded << '\n';
    std::cout << "successor_attempts " << res.stats.successor_attempts << '\n';
    std::cout << "successor_item_scans " << res.stats.successor_item_scans << '\n';
    const double scans_per_expanded = res.stats.states_expanded == 0 ? 0.0 :
        static_cast<double>(res.stats.successor_item_scans) /
            static_cast<double>(res.stats.states_expanded);
    std::cout << std::fixed << std::setprecision(6)
              << "successor_scans_per_expanded_state " << scans_per_expanded << '\n';
    std::cout << "backfill_attempts " << res.stats.backfill_attempts << '\n';
    std::cout << "candidates_stored " << res.stats.candidates_stored << '\n';
    std::cout << "computed_window_collisions "
              << res.stats.computed_window_collisions << '\n';
    std::cout << "computed_window_replacements "
              << res.stats.computed_window_replacements << '\n';
    std::cout << "computed_window_rejections "
              << res.stats.computed_window_rejections << '\n';
    std::cout << "computed_window_index_collisions "
              << res.stats.computed_window_index_collisions << '\n';
    const double active_items_average = res.stats.active_item_samples == 0 ? 0.0 :
        static_cast<double>(res.stats.active_items_sum) /
            static_cast<double>(res.stats.active_item_samples);
    std::cout << "active_items_average_on_expansion " << active_items_average << '\n';
    std::cout << "active_items_max_on_expansion " << res.stats.active_items_max << '\n';
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
    std::cout << "items_considered_for_introduction "
              << res.stats.items_considered_for_introduction << '\n';
    std::cout << "items_introduced " << res.stats.items_introduced << '\n';
    std::cout << "items_rejected_by_envelope " << res.stats.items_rejected_by_envelope << '\n';
    std::cout << "items_rejected_by_bound " << res.stats.items_rejected_by_bound << '\n';
    const double backfills_per_introduced = res.stats.items_introduced == 0 ? 0.0 :
        static_cast<double>(res.stats.backfill_attempts) /
            static_cast<double>(res.stats.items_introduced);
    std::cout << "backfills_per_item_introduced " << backfills_per_introduced << '\n';
    for (std::size_t decile = 0;
         decile < res.stats.items_introduced_by_capacity_decile.size(); ++decile) {
        std::cout << "items_introduced_capacity_percent " << decile * 10 << ' '
                  << (decile + 1) * 10 << ' '
                  << res.stats.items_introduced_by_capacity_decile[decile] << '\n';
        std::cout << "items_introduced_reduction_range_percent " << decile * 10 << ' '
                  << (decile + 1) * 10 << ' '
                  << res.stats.items_introduced_by_reduction_decile[decile] << '\n';
    }
    std::cout << "bound_winner " << res.stats.bound_winner << '\n';
    std::cout << "paper_faithful_mode " << (opt.paper_faithful_mode ? 1 : 0) << '\n';
    std::cout << "global_bound_used " << res.stats.global_bound_used << '\n';
    std::cout << "dp_stop_reason " << res.stats.dp_stop_reason << '\n';
    for (const auto& [type, calls] : res.stats.contextual_bound_calls) {
        std::cout << "contextual_bound_calls " << type << ' ' << calls << '\n';
    }
    std::cout << "contextual_bound_state_queries "
              << res.stats.contextual_bound_state_queries << '\n';
    std::cout << "contextual_bound_item_queries "
              << res.stats.contextual_bound_item_queries << '\n';
    std::cout << "contextual_bound_calls_avoided_by_lower "
              << res.stats.contextual_bound_calls_avoided_by_lower << '\n';
    std::cout << "contextual_bound_state_calls_avoided_by_lower "
              << res.stats.contextual_bound_state_calls_avoided_by_lower << '\n';
    std::cout << "contextual_bound_item_calls_avoided_by_lower "
              << res.stats.contextual_bound_item_calls_avoided_by_lower << '\n';
    constexpr std::array<const char*, 4> contextual_types{
        "U3", "V", "TauStar", "BestItemStar"};
    for (const char* type : contextual_types) {
        const auto value_or_zero = [type](const auto& values) {
            const auto found = values.find(type);
            return found == values.end() ? 0LL : found->second;
        };
        std::cout << "contextual_bound_evaluations " << type << ' '
                  << value_or_zero(res.stats.contextual_bound_evaluations) << '\n';
        std::cout << "contextual_bound_wins " << type << ' '
                  << value_or_zero(res.stats.contextual_bound_wins) << '\n';
        std::cout << "contextual_bound_state_wins " << type << ' '
                  << value_or_zero(res.stats.contextual_bound_state_wins) << '\n';
        std::cout << "contextual_bound_item_wins " << type << ' '
                  << value_or_zero(res.stats.contextual_bound_item_wins) << '\n';
        std::cout << "contextual_bound_fathoms " << type << ' '
                  << value_or_zero(res.stats.contextual_bound_fathoms) << '\n';
    }
    std::cout << "bound_context_rebuilds " << res.stats.bound_context_rebuilds << '\n';
    std::cout << "bound_context_items_processed "
              << res.stats.bound_context_items_processed << '\n';
    const double average_context_items = res.stats.bound_context_rebuilds == 0 ? 0.0 :
        static_cast<double>(res.stats.bound_context_items_processed) /
            static_cast<double>(res.stats.bound_context_rebuilds);
    std::cout << "bound_context_average_items " << average_context_items << '\n';
    std::cout << "bound_context_tau_q_recomputations "
              << res.stats.bound_context_tau_q_recomputations << '\n';
    std::cout << "bound_context_tau_q_items_scanned "
              << res.stats.bound_context_tau_q_items_scanned << '\n';
    std::cout << "bound_context_best_q_recomputations "
              << res.stats.bound_context_best_q_recomputations << '\n';
    std::cout << "bound_context_best_q_items_scanned "
              << res.stats.bound_context_best_q_items_scanned << '\n';
    std::cout << "bound_context_alpha_recomputations "
              << res.stats.bound_context_alpha_recomputations << '\n';
    std::cout << "bound_context_alpha_items_scanned "
              << res.stats.bound_context_alpha_items_scanned << '\n';
    std::cout << "bound_context_dominance_full_searches "
              << res.stats.bound_context_dominance_full_searches << '\n';
    std::cout << "bound_context_dominance_searches_avoided_by_witness "
              << res.stats.bound_context_dominance_searches_avoided_by_witness << '\n';
    std::cout << "bound_context_dominance_witness_invalidations "
              << res.stats.bound_context_dominance_witness_invalidations << '\n';
    std::cout << "bound_context_dominance_pair_checks "
              << res.stats.bound_context_dominance_pair_checks << '\n';
    if (verbose) {
        for (const SliceStats& slice : res.stats.slices) {
            std::cout << "slice begin=" << slice.begin << " end=" << slice.end
                      << " states_entered=" << slice.states_entered
                      << " states_expanded=" << slice.states_expanded
                      << " successor_attempts=" << slice.successor_attempts
                      << " successor_item_scans=" << slice.successor_item_scans
                      << " backfill_attempts=" << slice.backfill_attempts
                      << " states_created=" << slice.states_created
                      << " states_kept=" << slice.states_kept
                      << " states_fathomed_by_bound=" << slice.states_fathomed_by_bound
                      << " items_removed_threshold=" << slice.items_removed_threshold
                      << " active_items_before=" << slice.active_items_before
                      << " active_items_after=" << slice.active_items_after
                      << " items_considered_for_introduction="
                      << slice.items_considered_for_introduction
                      << " items_introduced=" << slice.items_introduced
                      << " items_rejected_by_envelope=" << slice.items_rejected_by_envelope
                      << " items_rejected_by_bound=" << slice.items_rejected_by_bound
                      << " contextual_bound_used=" << slice.contextual_bound_used << '\n';
        }
    }
    std::cout << "periodicity_level " << res.stats.periodicity_level << '\n';
    std::cout << "dp_capacity_processed " << res.stats.dp_capacity_processed << '\n';
    const double processed_capacity_percent = inst.capacity == 0 ? 0.0 :
        100.0 * static_cast<double>(res.stats.dp_capacity_processed) /
            static_cast<double>(inst.capacity);
    std::cout << "dp_capacity_processed_percent " << processed_capacity_percent << '\n';
    std::cout << "active_items_at_periodicity "
              << res.stats.active_items_at_periodicity << '\n';
    std::cout << "stop_reason " << res.stats.stop_reason << '\n';
    return 0;
}
