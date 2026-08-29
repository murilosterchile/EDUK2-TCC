#include "ukp/io.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/terminating_step_off.hpp"
#include "ukp/verify.hpp"

#include "../optimized/instance_features.hpp"
#include "../optimized/kernel_dispatcher.hpp"
#include "../optimized/preprocessing.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ukp;

namespace {

using Clock = std::chrono::steady_clock;

template <typename Function>
long long elapsed_ns(Function&& function) {
    const auto start = Clock::now();
    function();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - start).count();
}

long long median_ns(std::vector<long long> samples) {
    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2;
    if (samples.size() % 2 != 0) return samples[middle];
    return samples[middle - 1] + (samples[middle] - samples[middle - 1]) / 2;
}

const char* classification(double ratio) {
    if (ratio > 1.5) return "TSO much faster";
    if (ratio > 1.1) return "TSO faster";
    if (ratio >= 1.0 / 1.1) return "approximately tied";
    if (ratio >= 1.0 / 1.5) return "EDUK2 faster";
    return "EDUK2 much faster";
}

const char* kernel_name(optimized::detail::KernelChoice kernel) {
    return kernel == optimized::detail::KernelChoice::Tso ? "TSO" : "EDUK2";
}

struct Arguments {
    int repetitions = 15;
    long long max_transitions = 0;
    std::vector<std::string> instances;
};

Arguments parse_arguments(int argc, char** argv) {
    Arguments arguments;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--repetitions") {
            if (++i >= argc) throw std::invalid_argument("--repetitions requires N");
            arguments.repetitions = std::stoi(argv[i]);
            if (arguments.repetitions <= 0) {
                throw std::invalid_argument("--repetitions must be positive");
            }
        } else if (argument == "--tso-max-transitions") {
            if (++i >= argc) {
                throw std::invalid_argument("--tso-max-transitions requires N");
            }
            arguments.max_transitions = std::stoll(argv[i]);
            if (arguments.max_transitions < 0) {
                throw std::invalid_argument("--tso-max-transitions must be nonnegative");
            }
        } else {
            arguments.instances.push_back(argument);
        }
    }
    if (arguments.instances.empty()) {
        throw std::invalid_argument("no instance files supplied");
    }
    return arguments;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        std::cout
            << "instance,repetitions,n,C,n_after_common,reduction_ratio,min_weight,"
               "max_weight,w_best,p_best,best_efficiency,C_over_w_best,"
               "C_over_min_weight,gcd,best_second_efficiency_gap,"
               "near_best_efficiency_items,mean_weight,weight_variance,"
               "median_eduk2_ns,median_tso_ns,median_auto_ns,speedup_tso,"
               "speedup_auto,classification,profit_equal,tso_status,"
               "tso_work_consumed,tso_max_transitions,dispatch_kernel,"
               "dispatch_reason,residual_pressure,best_weight_orientation,"
               "dispatch_estimated_tso_work,dispatch_estimated_tso_bytes,"
               "auto_effective_kernel,tso_budget_exhausted_derived\n";

        SolverOptions eduk2_options;
        eduk2_options.use_kernel_dispatcher = false;
        optimized::Solver eduk2_solver(eduk2_options);

        SolverOptions auto_options;
        auto_options.tso_max_transitions = arguments.max_transitions;
        optimized::Solver auto_solver(auto_options);

        optimized::TsoOptions tso_options;
        tso_options.max_transitions = arguments.max_transitions;
        const optimized::TerminatingStepOff tso_solver(tso_options);

        for (const std::string& filename : arguments.instances) {
            const Instance instance = read_instance_file(filename);

            // Full features remain useful for analysis. They are deliberately
            // outside all timed regions.
            const std::vector<Item> common =
                optimized::detail::common_preprocess_items(instance);
            const InstanceFeatures features =
                optimized::detail::extract_instance_features(instance, common);

            // The production dispatcher is queried directly. ukp_kernel_bench
            // links ukp_optimized_none, so Stats fields are compile-time zero
            // and must not be used to infer the selected kernel.
            const InstanceFeatures dispatch_features =
                optimized::detail::extract_dispatch_features(instance);
            optimized::TsoOptions dispatch_options;
            dispatch_options.max_dp_bytes = auto_options.tso_max_dp_bytes;
            dispatch_options.max_transitions = arguments.max_transitions;
            const optimized::detail::DispatchDecision dispatch =
                optimized::detail::dispatch_kernel(dispatch_features, dispatch_options);

            std::vector<long long> eduk2_times;
            std::vector<long long> tso_times;
            std::vector<long long> auto_times;
            eduk2_times.reserve(arguments.repetitions);
            tso_times.reserve(arguments.repetitions);
            auto_times.reserve(arguments.repetitions);
            SolverResult eduk2;
            SolverResult automatic;
            optimized::TsoResult tso;

            const auto run_eduk2 = [&] {
                eduk2_times.push_back(elapsed_ns([&] {
                    eduk2 = eduk2_solver.solve(instance);
                }));
            };
            const auto run_tso = [&] {
                tso_times.push_back(elapsed_ns([&] {
                    tso = tso_solver.solve(instance);
                }));
            };
            const auto run_auto = [&] {
                auto_times.push_back(elapsed_ns([&] {
                    automatic = auto_solver.solve(instance);
                }));
            };

            for (int repetition = 0; repetition < arguments.repetitions; ++repetition) {
                switch (repetition % 4) {
                    case 0: run_eduk2(); run_tso(); run_auto(); break;
                    case 1: run_tso(); run_eduk2(); run_auto(); break;
                    case 2: run_auto(); run_eduk2(); run_tso(); break;
                    default: run_auto(); run_tso(); run_eduk2(); break;
                }
                if (!verify_solution(instance, eduk2.solution) ||
                    !verify_solution(instance, automatic.solution) ||
                    eduk2.solution.profit != automatic.solution.profit) {
                    throw std::runtime_error(filename + ": EDUK2/AUTO divergence");
                }
                if (tso.status == optimized::TsoStatus::ProvedOptimal &&
                    (!verify_solution(instance, tso.solution) ||
                     tso.solution.profit != eduk2.solution.profit)) {
                    throw std::runtime_error(filename + ": TSO/EDUK2 divergence");
                }
            }

            const long long median_eduk2 = median_ns(eduk2_times);
            const long long median_tso = median_ns(tso_times);
            const long long median_auto = median_ns(auto_times);
            const bool tso_applicable =
                tso.status == optimized::TsoStatus::ProvedOptimal;
            const double speedup_auto = median_auto == 0 ? 0.0 :
                static_cast<double>(median_eduk2) / median_auto;
            const double speedup_tso = !tso_applicable || median_tso == 0 ? 0.0 :
                static_cast<double>(median_eduk2) / median_tso;

            const bool dispatcher_attempts_tso =
                dispatch.kernel == optimized::detail::KernelChoice::Tso;
            const bool derived_budget_exhausted =
                dispatcher_attempts_tso &&
                tso.status == optimized::TsoStatus::WorkBudgetExceeded;
            const char* auto_effective_kernel = !dispatcher_attempts_tso
                ? "EDUK2"
                : (tso.status == optimized::TsoStatus::ProvedOptimal
                    ? "TSO" : "EDUK2_FALLBACK");

            std::cout << std::filesystem::path(filename).filename().string() << ','
                      << arguments.repetitions << ',' << instance.items.size() << ','
                      << instance.capacity << ',' << common.size() << ','
                      << features.common_reduction_ratio << ',' << features.min_weight << ','
                      << features.max_weight << ',' << features.best_item_weight << ','
                      << features.best_item_profit << ',' << features.best_item_efficiency << ','
                      << features.capacity_over_best_weight << ','
                      << features.capacity_over_min_weight << ',' << features.weight_gcd << ','
                      << features.best_second_efficiency_gap << ','
                      << features.near_best_efficiency_items << ',' << features.mean_weight << ','
                      << features.weight_variance << ',' << median_eduk2 << ','
                      << median_tso << ',' << median_auto << ',';

            if (tso_applicable) std::cout << speedup_tso;
            else std::cout << "NA";
            std::cout << ',' << speedup_auto << ','
                      << (tso_applicable ? classification(speedup_tso)
                                         : "kernel_not_applicable")
                      << ",1," << tso.status_message << ','
                      << tso.telemetry.transitions_considered << ','
                      << arguments.max_transitions << ','
                      << kernel_name(dispatch.kernel) << ',' << dispatch.reason << ','
                      << static_cast<double>(dispatch.residual_pressure) << ','
                      << dispatch.orientation_name << ','
                      << static_cast<double>(dispatch.estimated_tso_work) << ','
                      << dispatch.estimated_tso_bytes << ','
                      << auto_effective_kernel << ','
                      << (derived_budget_exhausted ? 1 : 0) << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ukp_kernel_bench: " << error.what() << '\n';
        std::cerr << "usage: ukp_kernel_bench [--repetitions N] "
                     "[--tso-max-transitions N] <instance.ukp>...\n";
        return 2;
    }
}