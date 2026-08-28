#include "ukp/faithful_solver.hpp"
#include "ukp/generator.hpp"
#include "ukp/io.hpp"
#include "ukp/terminating_step_off.hpp"
#include "ukp/verify.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace ukp;

namespace {

void require_tso(const Instance& instance, Profit expected, const std::string& label) {
    const optimized::TsoResult result = optimized::TerminatingStepOff().solve(instance);
    if (result.status != optimized::TsoStatus::ProvedOptimal)
        throw std::runtime_error(label + ": TSO unexpectedly not applicable");
    if (result.solution.profit != expected)
        throw std::runtime_error(label + ": profit divergence");
    if (!verify_solution(instance, result.solution))
        throw std::runtime_error(label + ": reconstruction divergence");
}

void require_portfolio(const Instance& instance, const std::string& label) {
    const Profit expected = dense_dp_value(instance);
    SolverOptions forced_options;
    forced_options.use_kernel_dispatcher = false;
    const SolverResult forced = optimized::Solver(forced_options).solve(instance);
    const SolverResult automatic = optimized::Solver().solve(instance);
    if (!verify_solution(instance, forced.solution) ||
        forced.solution.weight > instance.capacity ||
        forced.solution.profit != expected) {
        throw std::runtime_error(label + ": forced EDUK2 divergence");
    }
    if (!verify_solution(instance, automatic.solution) ||
        automatic.solution.weight > instance.capacity ||
        automatic.solution.profit != expected) {
        throw std::runtime_error(label + ": AUTO divergence");
    }
    require_tso(instance, expected, label);
}

void require_invalid(const Instance& instance, const std::string& label) {
    bool auto_rejected = false;
    bool tso_rejected = false;
    try {
        (void)optimized::Solver().solve(instance);
    } catch (const std::invalid_argument&) {
        auto_rejected = true;
    }
    try {
        (void)optimized::TerminatingStepOff().solve(instance);
    } catch (const std::invalid_argument&) {
        tso_rejected = true;
    }
    if (!auto_rejected || !tso_rejected) {
        throw std::runtime_error(label + ": invalid input was accepted");
    }
}

}  // namespace

int main() {
    try {
        // Large deterministic suite with an independent dense oracle.
        for (unsigned seed = 1; seed <= 50'000; ++seed) {
            const Instance instance = make_random_instance(
                1 + seed % 20, 1, 40, seed % 121, seed);
            require_portfolio(instance, "random seed " + std::to_string(seed));
        }

        const Profit large_profit = std::numeric_limits<Profit>::max() / 4;
        const std::vector<std::pair<std::string, Instance>> edge_cases{
            {"one item", {17, {{0, 5, 11}}}},
            {"repeated weights", {25, {{0, 5, 7}, {1, 5, 9}, {2, 8, 12}}}},
            {"repeated profits", {25, {{0, 4, 9}, {1, 5, 9}, {2, 7, 9}}}},
            {"equal efficiencies", {29, {{0, 4, 12}, {1, 6, 18}, {2, 10, 30}}}},
            {"zero capacity", {0, {{0, 1, 1}, {1, 3, 7}}}},
            {"all overweight", {3, {{0, 4, 10}, {1, 9, 30}}}},
            {"nonmultiple best weight", {17, {{0, 6, 13}, {1, 5, 10}, {2, 8, 15}}}},
            {"type limits", {3, {{0, 1, large_profit},
                                  {1, std::numeric_limits<Weight>::max(), 1}}}},
            {"discard nonpositive", {11, {{0, 0, 0}, {1, 0, -2},
                                           {2, 3, 5}, {3, 4, 0}, {4, 5, -1}}}},
        };
        for (const auto& [label, instance] : edge_cases) {
            require_portfolio(instance, label);
        }
        require_invalid({10, {{0, -1, 5}}}, "negative weight");
        require_invalid({0, {{0, 0, 1}}}, "zero-weight positive-profit");

        std::size_t corpus_checked = 0;
        const auto data = std::filesystem::path(UKP_SOURCE_DIR) / "data";
        for (const auto& entry : std::filesystem::directory_iterator(data)) {
            if (entry.path().extension() != ".ukp") continue;
            const Instance instance = read_instance_file(entry.path().string());
            const optimized::TsoResult tso = optimized::TerminatingStepOff().solve(instance);
            if (tso.status == optimized::TsoStatus::KernelNotApplicable) continue;
            const SolverResult faithful_result = faithful::Solver().solve(instance);
            require_tso(instance, faithful_result.solution.profit,
                        entry.path().filename().string());
            if (!verify_solution(instance, faithful_result.solution) ||
                faithful_result.solution.weight > instance.capacity) {
                throw std::runtime_error(
                    entry.path().filename().string() + ": faithful reconstruction divergence");
            }
            ++corpus_checked;
        }
        if (corpus_checked == 0) throw std::runtime_error("no corpus case was TSO-applicable");
        std::cout << "50000 random portfolio/oracle checks and corpus checks passed; "
                     "divergences=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
