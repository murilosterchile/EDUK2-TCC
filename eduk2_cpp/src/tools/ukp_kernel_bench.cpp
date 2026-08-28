#include "ukp/io.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/terminating_step_off.hpp"
#include "ukp/verify.hpp"

#include "../optimized/instance_features.hpp"
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

struct Arguments {
    int repetitions = 15;
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
        std::cout << "instance,repetitions,n,C,n_after_common,reduction_ratio,min_weight,"
                     "max_weight,w_best,p_best,best_efficiency,C_over_w_best,"
                     "C_over_min_weight,gcd,best_second_efficiency_gap,"
                     "near_best_efficiency_items,mean_weight,weight_variance,"
                     "median_eduk2_ns,median_tso_ns,median_auto_ns,speedup_tso,"
                     "speedup_auto,classification,profit_equal\n";

        SolverOptions eduk2_options;
        eduk2_options.use_kernel_dispatcher = false;
        optimized::Solver eduk2_solver(eduk2_options);
        optimized::Solver auto_solver;
        const optimized::TerminatingStepOff tso_solver;

        for (const std::string& filename : arguments.instances) {
            const Instance instance = read_instance_file(filename);
            const std::vector<Item> common =
                optimized::detail::common_preprocess_items(instance);
            const InstanceFeatures features =
                optimized::detail::extract_instance_features(instance, common);

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
                eduk2_times.push_back(elapsed_ns([&] { eduk2 = eduk2_solver.solve(instance); }));
            };
            const auto run_tso = [&] {
                tso_times.push_back(elapsed_ns([&] { tso = tso_solver.solve(instance); }));
            };
            const auto run_auto = [&] {
                auto_times.push_back(elapsed_ns([&] { automatic = auto_solver.solve(instance); }));
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
            const long long median_auto = median_ns(auto_times);
            const bool tso_applicable =
                tso.status == optimized::TsoStatus::ProvedOptimal;
            const long long median_tso = tso_applicable ? median_ns(tso_times) : 0;
            const double speedup_auto = median_auto == 0 ? 0.0 :
                static_cast<double>(median_eduk2) / median_auto;

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
                      << features.weight_variance << ',' << median_eduk2 << ',';
            if (!tso_applicable) {
                std::cout << "NA," << median_auto << ",NA," << speedup_auto
                          << ",kernel_not_applicable,1\n";
                continue;
            }
            const double speedup_tso = median_tso == 0 ? 0.0 :
                static_cast<double>(median_eduk2) / median_tso;
            std::cout << median_tso << ',' << median_auto << ',' << speedup_tso << ','
                      << speedup_auto << ',' << classification(speedup_tso) << ",1\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ukp_kernel_bench: " << error.what() << '\n';
        std::cerr << "usage: ukp_kernel_bench [--repetitions N] <instance.ukp>...\n";
        return 2;
    }
}
