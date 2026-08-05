#include "ukp/faithful_solver.hpp"
#include "ukp/generator.hpp"
#include "ukp/optimized_solver.hpp"
#include "ukp/verify.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace ukp;

static long long ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

static void run_ocaml(const Instance& inst, int n, Profit reference_profit) {
    constexpr const char* executable = "/home/aprix/Downloads/pyasukp_mail/pyasukp/pyasukpbct";
    if (!std::filesystem::is_regular_file(executable)) return;
    const std::string input = "/tmp/ukp_bench_" + std::to_string(getpid()) + "_" + std::to_string(n) + ".dat";
    {
        std::ofstream out(input);
        out << "n: " << inst.items.size() << "\nc: " << inst.capacity << "\nbegin data\n";
        for (const Item& item : inst.items) out << item.w << ' ' << item.p << '\n';
        out << "end data\n";
    }
    const std::string command = std::string(executable) + " -src " + input + " -batch 2>&1";
    const auto t0 = std::chrono::steady_clock::now();
    FILE* pipe = popen(command.c_str(), "r");
    std::string output;
    if (pipe != nullptr) {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
        pclose(pipe);
    }
    std::filesystem::remove(input);
    std::istringstream fields(output);
    std::vector<std::string> tokens;
    for (std::string token; fields >> token;) tokens.push_back(token);
    long long remaining = 0, points = 0, profit = 0, nodes = 0;
    if (tokens.size() >= 8) {
        const std::size_t first = tokens.size() - 8;
        const std::string& periodic = tokens[first + 1];
        try {
            (void)std::stoll(tokens[first]);
            remaining = std::stoll(tokens[first + 2]);
            points = std::stoll(tokens[first + 3]);
            profit = std::stoll(tokens[first + 5]);
            nodes = std::stoll(tokens[first + 7]);
        } catch (const std::exception&) {
            std::cerr << "warning: could not parse numeric pyasukpbct output for n=" << n << '\n';
            return;
        }
        std::cout << "sc," << n << ',' << inst.capacity << ",ocaml_pyasukpbct," << profit << ','
                  << (profit == reference_profit) << ',' << ms_since(t0) << ',' << nodes << ",0," << points
                  << ",0," << remaining << ",0,ocaml_" << periodic << '\n';
    } else {
        std::cerr << "warning: could not parse pyasukpbct batch output for n=" << n
                  << ": " << output;
    }
}

int main(int argc, char** argv) {
    const bool use_ocaml = argc > 1 && std::string(argv[1]) == "--ocaml";
    std::cout << "family,n,c,solver,profit,verified,time_ms,bb_nodes,points_generated,points_kept,states_fathomed,items_after,state_bytes_approx,stop_reason\n";
    for (int n : {50, 100, 200}) {
        Instance inst = make_strongly_correlated(n, 1000, 5, 50000 + n * 100);
        auto run_faithful = [&](const char* label, SolverOptions options) {
            const auto t0 = std::chrono::steady_clock::now();
            const auto result = faithful::Solver(options).solve(inst);
            std::cout << "sc," << n << ',' << inst.capacity << ',' << label << ','
                      << result.solution.profit << ',' << verify_solution(inst, result.solution) << ','
                      << ms_since(t0) << ',' << result.stats.bb_nodes << ','
                      << result.stats.points_generated << ',' << result.stats.states_kept << ','
                      << result.stats.states_fathomed << ',' << result.stats.after_preprocess_items << ','
                      << result.stats.estimated_state_bytes << ',' << result.stats.stop_reason << '\n';
        };
        SolverOptions critical_only;
        critical_only.use_bounds = false;
        critical_only.use_core_bb = false;
        run_faithful("faithful_critical", critical_only);
        SolverOptions with_bounds;
        with_bounds.use_core_bb = false;
        run_faithful("faithful_bounds", with_bounds);
        SolverOptions with_bb;
        run_faithful("faithful_bb", with_bb);

        SolverOptions opt;
        const auto t0 = std::chrono::steady_clock::now();
        auto b = optimized::Solver(opt).solve(inst);
        auto tb = ms_since(t0);
        std::cout << "sc," << n << ',' << inst.capacity << ",optimized," << b.solution.profit << ','
                  << verify_solution(inst, b.solution) << ',' << tb << ',' << b.stats.bb_nodes << ','
                  << b.stats.points_generated << ',' << b.stats.states_kept << ','
                  << b.stats.states_fathomed << ',' << b.stats.after_preprocess_items << ','
                  << b.stats.estimated_state_bytes << ",optimized\n";
        if (use_ocaml) run_ocaml(inst, n, b.solution.profit);
    }
    return 0;
}
