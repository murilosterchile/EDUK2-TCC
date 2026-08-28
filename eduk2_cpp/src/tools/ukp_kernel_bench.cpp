#include "ukp/io.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/terminating_step_off.hpp"
#include "ukp/verify.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

using namespace ukp;

namespace {

long long elapsed_us(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
}

const char* classification(double ratio) {
    if (ratio > 1.5) return "TSO much faster";
    if (ratio > 1.1) return "TSO faster";
    if (ratio >= 1.0 / 1.1) return "approximately tied";
    if (ratio >= 1.0 / 1.5) return "EDUK2 faster";
    return "EDUK2 much faster";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ukp_kernel_bench <instance.ukp>...\n";
        return 2;
    }
    std::cout << "instance,n,C,n_after_common,w_best,C_over_w_best,gcd,"
                 "eduk2_us,tso_us,eduk2_over_tso,classification,profit_equal\n";
    for (int i = 1; i < argc; ++i) {
        const Instance instance = read_instance_file(argv[i]);
        std::vector<Item> common;
        Weight gcd = 0;
        for (const Item& item : instance.items) {
            if (item.w > 0 && item.p > 0 && item.w <= instance.capacity) {
                common.push_back(item);
                gcd = std::gcd(gcd, item.w);
            }
        }
        Item best;
        if (!common.empty()) best = *std::max_element(
            common.begin(), common.end(),
            [](const Item& a, const Item& b) { return better_ratio(b, a); });

        const auto eduk2_start = std::chrono::steady_clock::now();
        const SolverResult eduk2 = optimized::Solver().solve(instance);
        const long long eduk2_us = elapsed_us(eduk2_start);
        const auto tso_start = std::chrono::steady_clock::now();
        const optimized::TsoResult tso = optimized::TerminatingStepOff().solve(instance);
        const long long tso_us = elapsed_us(tso_start);

        std::cout << std::filesystem::path(argv[i]).filename().string() << ','
                  << instance.items.size() << ',' << instance.capacity << ','
                  << common.size() << ',' << best.w << ','
                  << (best.w == 0 ? 0 : instance.capacity / best.w) << ',' << gcd << ','
                  << eduk2_us << ',';
        if (tso.status == optimized::TsoStatus::KernelNotApplicable) {
            std::cout << "NA,NA,kernel_not_applicable,NA\n";
            continue;
        }
        const double ratio = tso_us == 0 ? 0.0 :
            static_cast<double>(eduk2_us) / static_cast<double>(tso_us);
        std::cout << tso_us << ',' << ratio << ',' << classification(ratio) << ','
                  << (eduk2.solution.profit == tso.solution.profit &&
                      verify_solution(instance, tso.solution)) << '\n';
    }
}
