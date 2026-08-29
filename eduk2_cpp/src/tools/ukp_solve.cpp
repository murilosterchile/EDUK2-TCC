#include "ukp/faithful_solver.hpp"
#include "ukp/io.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/terminating_step_off.hpp"
#include "ukp/verify.hpp"

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

using namespace ukp;

namespace {

void print_basic_stats(const Instance& inst, const SolverResult& res,
                       const SolverOptions& options) {
    if constexpr (!stats_enabled_v<StatsMode::Basic>) {
        (void)inst;
        (void)res;
        (void)options;
        return;
    }

    std::cout << "items_original " << res.stats.original_items << '\n';
    std::cout << "kernel " << res.stats.selected_kernel << '\n';
    std::cout << "dispatch_reason " << res.stats.dispatch_reason << '\n';
    std::cout << "tso_attempted " << res.stats.tso_attempted << '\n';
    std::cout << "tso_work_budget " << res.stats.tso_work_budget << '\n';
    std::cout << "tso_work_consumed " << res.stats.tso_work_consumed << '\n';
    std::cout << "tso_budget_exhausted " << res.stats.tso_budget_exhausted << '\n';
    std::cout << "tso_fallback_to_eduk2 " << res.stats.tso_fallback_to_eduk2 << '\n';
    std::cout << "items_after_preprocess " << res.stats.after_preprocess_items << '\n';
    std::cout << "states_scanned " << res.stats.states_scanned << '\n';
    std::cout << "states_expanded " << res.stats.states_expanded << '\n';
    std::cout << "successor_attempts " << res.stats.successor_attempts << '\n';
    std::cout << "successor_item_scans " << res.stats.successor_item_scans << '\n';
    const double scans_per_expanded = res.stats.states_expanded == 0 ? 0.0 :
        static_cast<double>(res.stats.successor_item_scans) /
            static_cast<double>(res.stats.states_expanded);
    std::cout << std::fixed << std::setprecision(6)
              << "successor_scans_per_expanded_state "
              << scans_per_expanded << '\n';
    std::cout << "backfill_attempts " << res.stats.backfill_attempts << '\n';
    std::cout << "cursor_advances " << res.stats.cursor_advances << '\n';
    std::cout << "states_fathomed " << res.stats.states_fathomed << '\n';
    std::cout << "bound_calls " << res.stats.bound_calls << '\n';
    std::cout << "bb_nodes " << res.stats.bb_nodes << '\n';
    std::cout << "bb_branch_evaluations " << res.stats.bb_branch_evaluations << '\n';
    std::cout << "bb_fractional_bound_calls " << res.stats.bb_fractional_bound_calls << '\n';
    std::cout << "bb_fractional_bound_prunes " << res.stats.bb_fractional_bound_prunes << '\n';
    std::cout << "bb_u3_calls " << res.stats.bb_u3_calls << '\n';
    std::cout << "bb_u3_prunes " << res.stats.bb_u3_prunes << '\n';
    std::cout << "bb_strong_bound_calls " << res.stats.bb_strong_bound_calls << '\n';
    std::cout << "bb_strong_bound_prunes " << res.stats.bb_strong_bound_prunes << '\n';
    std::cout << "bb_work_stops " << res.stats.bb_work_stops << '\n';
    std::cout << "incumbent_before_cheap_heuristic "
              << res.stats.incumbent_before_cheap_heuristic << '\n';
    std::cout << "incumbent_after_cheap_heuristic "
              << res.stats.incumbent_after_cheap_heuristic << '\n';
    std::cout << "cheap_incumbent_candidates_tested "
              << res.stats.cheap_incumbent_candidates_tested << '\n';
    std::cout << "points_generated " << res.stats.points_generated << '\n';
    std::cout << "points_kept " << res.stats.states_kept << '\n';
    std::cout << "state_bytes_approx " << res.stats.estimated_state_bytes << '\n';
    std::cout << "items_removed_bound " << res.stats.items_removed_bound << '\n';
    std::cout << "items_removed_threshold " << res.stats.items_removed_threshold << '\n';
    std::cout << "items_removed_simple " << res.stats.items_removed_simple << '\n';
    std::cout << "items_removed_multiple " << res.stats.items_removed_multiple << '\n';
    std::cout << "items_removed_modular " << res.stats.items_removed_modular << '\n';
    std::cout << "items_removed_core_multiple "
              << res.stats.items_removed_core_multiple << '\n';
    std::cout << "incumbent_improvements_bb "
              << res.stats.incumbent_improvements_bb << '\n';
    std::cout << "incumbent_improvements_dp "
              << res.stats.incumbent_improvements_dp << '\n';
    std::cout << "greedy_completion_calls "
              << res.stats.greedy_completion_calls << '\n';
    std::cout << "greedy_completion_item_scans "
              << res.stats.greedy_completion_item_scans << '\n';
    std::cout << "greedy_completion_improvements "
              << res.stats.greedy_completion_improvements << '\n';
    std::cout << "greedy_completion_reconstruction_steps "
              << res.stats.greedy_completion_reconstruction_steps << '\n';
    std::cout << "bound_completion_calls "
              << res.stats.bound_completion_calls << '\n';
    std::cout << "bound_completion_improvements "
              << res.stats.bound_completion_improvements << '\n';
    std::cout << "bound_completion_u3_calls "
              << res.stats.bound_completion_u3_calls << '\n';
    std::cout << "bound_completion_v_calls "
              << res.stats.bound_completion_v_calls << '\n';
    std::cout << "bound_completion_both_calls "
              << res.stats.bound_completion_both_calls << '\n';
    std::cout << "bound_completion_reconstruction_steps "
              << res.stats.bound_completion_reconstruction_steps << '\n';
    std::cout << "active_items_final " << res.stats.active_items_final << '\n';
    std::cout << "items_considered_for_introduction "
              << res.stats.items_considered_for_introduction << '\n';
    std::cout << "items_introduced " << res.stats.items_introduced << '\n';
    std::cout << "items_rejected_by_envelope "
              << res.stats.items_rejected_by_envelope << '\n';
    std::cout << "items_rejected_by_bound "
              << res.stats.items_rejected_by_bound << '\n';
    const double backfills_per_introduced = res.stats.items_introduced == 0 ? 0.0 :
        static_cast<double>(res.stats.backfill_attempts) /
            static_cast<double>(res.stats.items_introduced);
    std::cout << "backfills_per_item_introduced "
              << backfills_per_introduced << '\n';
    std::cout << "bound_winner " << res.stats.bound_winner << '\n';
    std::cout << "pyasukp_bound_mode " << res.stats.pyasukp_bound_mode << '\n';
    std::cout << "paper_faithful_mode "
              << (options.paper_faithful_mode ? 1 : 0) << '\n';
    std::cout << "global_bound_used " << res.stats.global_bound_used << '\n';
    std::cout << "dp_stop_reason " << res.stats.dp_stop_reason << '\n';
    std::cout << "periodicity_level " << res.stats.periodicity_level << '\n';
    std::cout << "dp_capacity_processed "
              << res.stats.dp_capacity_processed << '\n';
    const double processed_capacity_percent = inst.capacity == 0 ? 0.0 :
        100.0 * static_cast<double>(res.stats.dp_capacity_processed) /
            static_cast<double>(inst.capacity);
    std::cout << "dp_capacity_processed_percent "
              << processed_capacity_percent << '\n';
    std::cout << "active_items_at_periodicity "
              << res.stats.active_items_at_periodicity << '\n';
    std::cout << "stop_reason " << res.stats.stop_reason << '\n';
}

void print_full_stats(const SolverResult& res, bool verbose) {
    if constexpr (!stats_enabled_v<StatsMode::Full>) {
        (void)res;
        (void)verbose;
        return;
    }

    std::cout << "phase_preprocessing_ns "
              << res.stats.phase_preprocessing_ns << '\n';
    std::cout << "phase_global_bounds_ns "
              << res.stats.phase_global_bounds_ns << '\n';
    std::cout << "phase_context_maintenance_ns "
              << res.stats.phase_context_maintenance_ns << '\n';
    std::cout << "phase_core_bb_ns "
              << res.stats.phase_core_bb_ns << '\n';
    std::cout << "phase_cheap_incumbent_ns "
              << res.stats.phase_cheap_incumbent_ns << '\n';
    std::cout << "phase_dp_ns " << res.stats.phase_dp_ns << '\n';
    std::cout << "phase_reconstruction_ns "
              << res.stats.phase_reconstruction_ns << '\n';
    std::cout << "phase_tso_speculation_ns "
              << res.stats.phase_tso_speculation_ns << '\n';

    std::cout << "historical_states_avoided "
              << res.stats.historical_states_avoided << '\n';
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
    std::cout << "active_items_average_on_expansion "
              << active_items_average << '\n';
    std::cout << "active_items_max_on_expansion "
              << res.stats.active_items_max << '\n';

    for (std::size_t decile = 0;
         decile < res.stats.items_introduced_by_capacity_decile.size(); ++decile) {
        std::cout << "items_introduced_capacity_percent " << decile * 10 << ' '
                  << (decile + 1) * 10 << ' '
                  << res.stats.items_introduced_by_capacity_decile[decile] << '\n';
        std::cout << "items_introduced_reduction_range_percent "
                  << decile * 10 << ' ' << (decile + 1) * 10 << ' '
                  << res.stats.items_introduced_by_reduction_decile[decile] << '\n';
    }

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
    std::cout << "lower_filter_hits "
              << res.stats.lower_filter_hits << '\n';
    std::cout << "bounds_short_circuited "
              << res.stats.bounds_short_circuited << '\n';
    std::cout << "active_items_checked "
              << res.stats.active_items_checked << '\n';
    std::cout << "active_items_removed_by_bound "
              << res.stats.active_items_removed_by_bound << '\n';
    std::cout << "successor_attempts_avoided "
              << res.stats.successor_attempts_avoided << '\n';
    std::cout << "cursor_advances_avoided "
              << res.stats.cursor_advances_avoided << '\n';

    constexpr std::array<const char*, 4> contextual_types{
        "U3", "V", "TauStar", "BestItemStar"};
    for (const char* type : contextual_types) {
        const auto value_or_zero = [type](const auto& values) {
            const auto found = values.find(type);
            return found == values.end() ? 0LL : found->second;
        };
        std::cout << "bounds_evaluated " << type << ' '
                  << value_or_zero(res.stats.bounds_evaluated) << '\n';
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

    std::cout << "bound_context_rebuilds "
              << res.stats.bound_context_rebuilds << '\n';
    std::cout << "bound_context_incremental_updates "
              << res.stats.bound_context_incremental_updates << '\n';
    std::cout << "bound_context_items_processed "
              << res.stats.bound_context_items_processed << '\n';
    const long long context_operations =
        res.stats.bound_context_rebuilds +
        res.stats.bound_context_incremental_updates;
    const double average_context_items = context_operations == 0 ? 0.0 :
        static_cast<double>(res.stats.bound_context_items_processed) /
            static_cast<double>(context_operations);
    std::cout << "bound_context_average_items "
              << average_context_items << '\n';
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
    std::cout << "residual_transactions "
              << res.stats.residual_transactions << '\n';
    std::cout << "residual_items_removed "
              << res.stats.residual_items_removed << '\n';
    std::cout << "context_rebuilds_requested "
              << res.stats.context_rebuilds_requested << '\n';
    std::cout << "context_rebuilds_skipped_no_change "
              << res.stats.context_rebuilds_skipped_no_change << '\n';
    std::cout << "suffix_rebuilds "
              << res.stats.suffix_rebuilds << '\n';
    std::cout << "duplicate_removal_requests "
              << res.stats.duplicate_removal_requests << '\n';

    if (!verbose) return;

    for (std::size_t item_id = 0;
         item_id < res.stats.backfill_attempts_by_item.size(); ++item_id) {
        const long long attempts = res.stats.backfill_attempts_by_item[item_id];
        if (attempts >= 0) {
            std::cout << "backfill_attempts_item " << item_id << ' '
                      << attempts << '\n';
        }
    }
    for (const SliceStats& slice : res.stats.slices) {
        std::cout << "slice begin=" << slice.begin << " end=" << slice.end
                  << " states_entered=" << slice.states_entered
                  << " states_expanded=" << slice.states_expanded
                  << " successor_attempts=" << slice.successor_attempts
                  << " successor_item_scans=" << slice.successor_item_scans
                  << " backfill_attempts=" << slice.backfill_attempts
                  << " cursor_advances=" << slice.cursor_advances
                  << " historical_states_avoided="
                  << slice.historical_states_avoided
                  << " states_created=" << slice.states_created
                  << " states_kept=" << slice.states_kept
                  << " states_fathomed_by_bound="
                  << slice.states_fathomed_by_bound
                  << " items_removed_threshold="
                  << slice.items_removed_threshold
                  << " active_items_before=" << slice.active_items_before
                  << " active_items_after=" << slice.active_items_after
                  << " items_considered_for_introduction="
                  << slice.items_considered_for_introduction
                  << " items_introduced=" << slice.items_introduced
                  << " items_rejected_by_envelope="
                  << slice.items_rejected_by_envelope
                  << " items_rejected_by_bound="
                  << slice.items_rejected_by_bound
                  << " contextual_bound_used="
                  << slice.contextual_bound_used << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr
            << "usage: ukp_solve <faithful|optimized> <instance-file>"
               " [--paper-faithful|--no-paper-faithful]"
               " [--simple-dominance] [--core-remainder-ordering]"
               " [--modular-dominance] [--core-multiple-dominance]"
               " [--bound-policy=u3|v|tau-star|best-item-star|best-certified|pyasukp-faithful]"
               " [--bound-completion=greedy|pyasukp]"
               " [--no-cheap-incumbent] [--cheap-incumbent-top-k=N]"
               " [--bb-strong-only|--bb-fractional] [--bb-u3]"
               " [--bb-work-budget=N]"
               " [--tso-max-transitions=N]"
               " [--kernel auto|eduk2|tso]"
               " [--verbose]\n";
        return 2;
    }

    const std::string solver = argv[1];
    const Instance inst = read_instance_file(argv[2]);
    SolverOptions options;
    bool verbose = false;
    std::string kernel = solver == "optimized" ? "auto" : "eduk2";
    optimized::TsoOptions tso_options;

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--kernel") {
            if (++i >= argc) {
                std::cerr << "--kernel requires auto, eduk2 or tso\n";
                return 2;
            }
            kernel = argv[i];
        } else if (arg.rfind("--kernel=", 0) == 0) {
            kernel = arg.substr(9);
        } else if (arg == "--no-tso-gcd-scaling") {
            tso_options.use_gcd_scaling = false;
        } else if (arg == "--tso-multiple-dominance") {
            tso_options.use_multiple_dominance = true;
        } else if (arg.rfind("--tso-max-transitions=", 0) == 0) {
            const long long budget = std::stoll(arg.substr(22));
            if (budget < 0) {
                std::cerr << "--tso-max-transitions must be nonnegative\n";
                return 2;
            }
            tso_options.max_transitions = budget;
            options.tso_max_transitions = budget;
        } else if (arg == "--paper-faithful") options.paper_faithful_mode = true;
        else if (arg == "--no-paper-faithful") options.paper_faithful_mode = false;
        else if (arg == "--simple-dominance") options.use_simple_dominance = true;
        else if (arg == "--core-remainder-ordering") {
            options.use_core_remainder_ordering = true;
        } else if (arg == "--modular-dominance") {
            options.use_modular_dominance = true;
        } else if (arg == "--core-multiple-dominance") {
            options.use_core_multiple_dominance = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--cheap-incumbent") {
            options.use_cheap_incumbent = true;
        } else if (arg == "--no-cheap-incumbent") {
            options.use_cheap_incumbent = false;
        } else if (arg.rfind("--cheap-incumbent-top-k=", 0) == 0) {
            options.cheap_incumbent_top_k = std::stoi(arg.substr(24));
        } else if (arg == "--bb-strong-only") {
            options.use_bb_fractional_bound = false;
            options.use_bb_u3_bound = false;
        } else if (arg == "--bb-fractional") {
            options.use_bb_fractional_bound = true;
        } else if (arg == "--bb-u3") {
            options.use_bb_u3_bound = true;
        } else if (arg.rfind("--bb-work-budget=", 0) == 0) {
            options.bb_work_budget = std::stoll(arg.substr(17));
        } else if (arg.rfind("--bound-policy=", 0) == 0) {
            const std::string value = arg.substr(15);
            if (value == "u3") options.bound_policy = BoundPolicy::U3;
            else if (value == "v") options.bound_policy = BoundPolicy::V;
            else if (value == "tau-star") options.bound_policy = BoundPolicy::TauStar;
            else if (value == "best-item-star") {
                options.bound_policy = BoundPolicy::BestItemStar;
            } else if (value == "best-certified") {
                options.bound_policy = BoundPolicy::BestCertified;
            } else if (value == "pyasukp-faithful") {
                options.bound_policy = BoundPolicy::PyasukpFaithful;
            } else {
                std::cerr << "unknown bound policy: " << value << '\n';
                return 2;
            }
        } else if (arg.rfind("--bound-completion=", 0) == 0) {
            const std::string value = arg.substr(19);
            if (value == "greedy") {
                options.use_pyasukp_bound_completion = false;
            } else if (value == "pyasukp") {
                options.use_pyasukp_bound_completion = true;
            } else {
                std::cerr << "unknown bound completion: " << value << '\n';
                return 2;
            }
        } else {
            std::cerr << "unknown option: " << arg << '\n';
            return 2;
        }
    }

    const auto start = std::chrono::steady_clock::now();
    if (solver == "optimized" && kernel == "tso") {
        const optimized::TsoResult tso =
            optimized::TerminatingStepOff(tso_options).solve(inst);
        const auto finish = std::chrono::steady_clock::now();
        std::cout << "kernel tso\nstatus " << tso.status_message << '\n';
        if (tso.status != optimized::TsoStatus::ProvedOptimal) return 3;
        write_solution(std::cout, tso.solution);
        std::cout << "verified " << (verify_solution(inst, tso.solution) ? 1 : 0) << '\n';
        std::cout << "time_us "
                  << std::chrono::duration_cast<std::chrono::microseconds>(finish - start).count()
                  << '\n';
        std::cout << "items_original " << tso.telemetry.original_items << '\n'
                  << "items_after_common_preprocess "
                  << tso.telemetry.after_common_preprocessing_items << '\n'
                  << "items_after_tso_preprocess "
                  << tso.telemetry.after_tso_preprocessing_items << '\n'
                  << "best_item_weight " << tso.telemetry.best_item_weight << '\n'
                  << "capacity_over_best_weight "
                  << (tso.telemetry.best_item_weight == 0 ? 0 :
                      inst.capacity / tso.telemetry.best_item_weight) << '\n'
                  << "weight_gcd " << tso.telemetry.weight_gcd << '\n'
                  << "original_capacity " << tso.telemetry.original_capacity << '\n'
                  << "scaled_capacity " << tso.telemetry.scaled_capacity << '\n'
                  << "gcd_scale_factor " << tso.telemetry.gcd_scale_factor << '\n'
                  << "states_scanned " << tso.telemetry.states_scanned << '\n'
                  << "transitions_considered "
                  << tso.telemetry.transitions_considered << '\n'
                  << "termination_level " << tso.telemetry.termination_level << '\n'
                  << "terminated_early " << (tso.telemetry.terminated_early ? 1 : 0) << '\n'
                  << "state_bytes_approx " << tso.telemetry.estimated_dp_bytes << '\n';
        std::cout << "estimated_dp_bytes_before_scaling "
                  << tso.telemetry.estimated_dp_bytes_before_scaling << '\n'
                  << "estimated_dp_bytes_after_scaling "
                  << tso.telemetry.estimated_dp_bytes_after_scaling << '\n';
        return 0;
    }
    if (kernel != "auto" && kernel != "eduk2") {
        std::cerr << "unknown kernel: " << kernel << '\n';
        return 2;
    }
    if (solver == "faithful" && kernel == "auto") {
        std::cerr << "--kernel auto is only available for optimized\n";
        return 2;
    }
    if (kernel == "eduk2") options.use_kernel_dispatcher = false;
    SolverResult result;
    if (solver == "faithful") {
        result = faithful::Solver(options).solve(inst);
    } else if (solver == "optimized") {
        result = optimized::Solver(options).solve(inst);
    } else {
        std::cerr << "unknown solver: " << solver << '\n';
        return 2;
    }
    const auto finish = std::chrono::steady_clock::now();

    write_solution(std::cout, result.solution);
    std::cout << "verified "
              << (verify_solution(inst, result.solution) ? 1 : 0) << '\n';
    std::cout << "time_ms "
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     finish - start).count()
              << '\n';
    std::cout << "time_us "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     finish - start).count()
              << '\n';
    std::cout << "stats_mode " << stats_mode_name() << '\n';

    print_basic_stats(inst, result, options);
    print_full_stats(result, verbose);

    if (verbose && !stats_enabled_v<StatsMode::Full>) {
        std::cerr << "warning: --verbose requires the full telemetry target\n";
    }
    return 0;
}
