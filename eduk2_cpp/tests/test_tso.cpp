#include "ukp/faithful_solver.hpp"
#include "ukp/generator.hpp"
#include "ukp/io.hpp"
#include "ukp/terminating_step_off.hpp"
#include "ukp/verify.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

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

}  // namespace

int main() {
    try {
        // Thousands of deterministic, independently generated small cases.
        for (unsigned seed = 1; seed <= 5000; ++seed) {
            const Instance instance = make_random_instance(
                1 + seed % 30, 1, 80, 100 + seed % 701, seed);
            require_tso(instance, dense_dp_value(instance),
                        "random seed " + std::to_string(seed));
        }

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
            ++corpus_checked;
        }
        if (corpus_checked == 0) throw std::runtime_error("no corpus case was TSO-applicable");
        std::cout << "TSO oracle and corpus checks passed; divergences=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
