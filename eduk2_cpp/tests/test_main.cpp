#include "ukp/bounds.hpp"
#include "ukp/dominance.hpp"
#include "ukp/faithful_solver.hpp"
#include "ukp/generator.hpp"
#include "ukp/io.hpp"
#include "ukp/verify.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ukp;

namespace {

constexpr Weight kDenseCapacityLimit = 20'000;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Configuration {
    const char* name;
    SolverOptions options;
};

std::vector<Configuration> faithful_configurations() {
    SolverOptions baseline;
    baseline.paper_faithful_mode = false;
    baseline.use_bounds = false;
    baseline.use_core_bb = false;
    baseline.use_periodicity = false;

    SolverOptions bounds;
    bounds.use_core_bb = false;
    bounds.use_periodicity = false;

    SolverOptions bb;
    bb.use_periodicity = false;

    return {{"baseline", baseline}, {"bounds", bounds}, {"branch_and_bound", bb},
            {"complete", SolverOptions{}}};
}

void check_faithful(const Instance& instance, Profit oracle, const std::string& label) {
    for (const Configuration& configuration : faithful_configurations()) {
        const SolverResult result = faithful::Solver(configuration.options).solve(instance);
        const std::string prefix = label + " [" + configuration.name + "]: ";
        require(verify_solution(instance, result.solution), prefix + "infeasible reconstruction");
        require(result.solution.weight <= instance.capacity, prefix + "weight exceeds capacity");
        require(result.solution.profit == oracle, prefix + "profit differs from dense oracle");
        require(result.stats.stop_reason != "uninitialized",
                prefix + "DP phase did not report a termination reason");
        if (configuration.name == std::string("complete")) {
            require(configuration.options.paper_faithful_mode, prefix + "default is not paper faithful");
            require(result.stats.items_removed_modular == 0, prefix + "paper mode used modular dominance");
            require(result.stats.items_removed_core_multiple == 0, prefix + "paper mode used core multiple dominance");
        }
        if (result.stats.dp_stop_reason != "not_started" && result.stats.dp_stop_reason != "empty_instance") {
            require(!result.stats.slices.empty(), prefix + "DP telemetry has no slices");
            long long fathomed = 0;
            long long threshold = 0;
            for (const SliceStats& slice : result.stats.slices) {
                require(slice.begin <= slice.end, prefix + "invalid slice range");
                fathomed += slice.states_fathomed_by_bound;
                threshold += slice.items_removed_threshold;
            }
            require(fathomed == result.stats.states_fathomed, prefix + "slice fathoming mismatch");
            require(threshold == result.stats.items_removed_threshold, prefix + "slice threshold mismatch");
            require(result.stats.slices.back().active_items_after == result.stats.active_items_final,
                    prefix + "final active-item mismatch");
        }
        if (!configuration.options.use_bounds) {
            require(result.stats.global_bound_used == "none", prefix + "disabled bounds reported a global bound");
            require(result.stats.contextual_bound_calls.empty(), prefix + "disabled bounds made contextual calls");
        }
    }
}

void check_faithful_switches() {
    const Instance instance{63, {{0, 5, 10}, {1, 6, 9}, {2, 8, 15}, {3, 11, 20}}};
    const Profit oracle = dense_dp_value(instance);
    const SolverResult paper = faithful::Solver(SolverOptions{}).solve(instance);
    require(paper.stats.items_removed_simple == 0, "paper mode enabled simple dominance");
    require(paper.stats.items_removed_multiple > 0, "mandatory best-item reduction was skipped");
    require(paper.solution.profit == oracle && verify_solution(instance, paper.solution), "paper switch result invalid");

    SolverOptions simple;
    simple.paper_faithful_mode = false;
    simple.use_simple_dominance = true;
    const SolverResult experimental = faithful::Solver(simple).solve(instance);
    require(experimental.stats.items_removed_simple > 0, "experimental simple dominance was not used");
    require(experimental.solution.profit == oracle && verify_solution(instance, experimental.solution),
            "experimental switch result invalid");

    SolverOptions forced_paper;
    forced_paper.use_simple_dominance = true;
    forced_paper.use_core_remainder_ordering = true;
    forced_paper.use_modular_dominance = true;
    forced_paper.use_core_multiple_dominance = true;
    const SolverResult forced = faithful::Solver(forced_paper).solve(instance);
    require(forced.stats.items_removed_simple == 0 && forced.stats.items_removed_modular == 0 &&
                forced.stats.items_removed_core_multiple == 0,
            "paper mode did not override extensions");

    const Instance core_instance{2900, {{0, 120, 300}, {1, 245, 580}, {2, 130, 301},
                                        {3, 260, 601}, {4, 310, 605}, {5, 194, 322},
                                        {6, 190, 310}}};
    const Profit core_oracle = dense_dp_value(core_instance);
    const SolverResult core_paper = faithful::Solver(SolverOptions{}).solve(core_instance);
    require(core_paper.stats.bb_nodes > 0, "core B&B was not entered");
    for (int extension = 0; extension != 3; ++extension) {
        SolverOptions option;
        option.paper_faithful_mode = false;
        if (extension == 0) option.use_core_remainder_ordering = true;
        if (extension == 1) option.use_modular_dominance = true;
        if (extension == 2) option.use_core_multiple_dominance = true;
        const SolverResult trial = faithful::Solver(option).solve(core_instance);
        require(verify_solution(core_instance, trial.solution) && trial.solution.profit == core_oracle,
                "isolated core extension changed the optimum");
    }
}

void check_paper_bound(const Instance& instance, Profit expected, bool tau, const char* name) {
    const auto items = remove_simple_dominated(instance.items);
    const auto context = make_bound_context(items);
    const BoundValue value = tau ? compute_tau_star(context, instance.capacity)
                                 : compute_best_item_star(context, instance.capacity);
    require(value.upper == expected, std::string(name) + ": published bound changed");
    require(value.upper >= dense_dp_value(instance), std::string(name) + ": invalid upper bound");
}

Instance subset_sum_family(unsigned seed) {
    Instance instance;
    instance.capacity = 300 + static_cast<Weight>(seed % 7);
    for (int i = 0; i < 18; ++i) {
        const Weight weight = 1 + ((i * 37 + static_cast<int>(seed) * 11) % 71);
        instance.items.push_back({i, weight, weight});
    }
    return instance;
}

Instance no_simple_dominance_family(unsigned seed) {
    Instance instance;
    instance.capacity = 450 + static_cast<Weight>(seed % 13);
    for (int i = 0; i < 20; ++i) {
        const Weight weight = 8 + 3 * i;
        // Profit strictly increases with weight, so simple dominance cannot remove an item.
        const Profit profit = 10 * weight + 1 + ((i * 17 + static_cast<int>(seed)) % 9);
        instance.items.push_back({i, weight, profit});
    }
    return instance;
}

Instance no_collective_dominance_family(unsigned seed) {
    Instance instance;
    instance.capacity = 350 + static_cast<Weight>(seed % 11);
    for (int i = 0; i < 16; ++i) {
        const Weight weight = 11 + 5 * i;
        const Profit profit = 2 * weight - (i % 3) + static_cast<int>(seed % 3);
        instance.items.push_back({i, weight, profit});
    }
    return instance;
}

void check_article_examples() {
    const Instance example_1{63, {{0, 15, 17}, {1, 20, 30}, {2, 25, 40}}};
    const Instance example_2{2900, {{0, 119, 119}, {1, 120, 297}, {2, 131, 309}}};
    const Instance example_3{2900, {{0, 120, 300}, {1, 245, 580}, {2, 130, 301},
                                    {3, 260, 601}, {4, 310, 605}, {5, 194, 322},
                                    {6, 190, 310}}};
    require(dense_dp_value(example_1) == 90, "article example 1 optimum changed");
    require(dense_dp_value(example_2) == 7140, "article example 2 optimum changed");
    require(dense_dp_value(example_3) == 7202, "article example 3 optimum changed");
    check_paper_bound(example_1, 99, false, "article example 1");
    check_paper_bound(example_2, 7149, false, "article example 2");
    check_paper_bound(example_3, 7205, true, "article example 3");
    check_faithful(example_1, 90, "article example 1");
    check_faithful(example_2, 7140, "article example 2");
    check_faithful(example_3, 7202, "article example 3");
}

void check_generated_families() {
    for (unsigned seed = 1; seed <= 8; ++seed) {
        const std::vector<std::pair<std::string, Instance>> families{
            {"random/" + std::to_string(seed), make_random_instance(20, 1, 100, 500, seed)},
            {"strong/" + std::to_string(seed), make_strongly_correlated(24, 10 + seed, 5, 700)},
            {"subset/" + std::to_string(seed), subset_sum_family(seed)},
            {"saw/" + std::to_string(seed), make_saw_like(20, 1, 80, 600, seed)},
            {"no-simple/" + std::to_string(seed), no_simple_dominance_family(seed)},
            {"no-collective/" + std::to_string(seed), no_collective_dominance_family(seed)},
        };
        for (const auto& [name, instance] : families) {
            if (name.rfind("no-simple/", 0) == 0) {
                require(remove_simple_dominated(instance.items).size() == instance.items.size(),
                        name + ": generator introduced simple dominance");
            }
            check_faithful(instance, dense_dp_value(instance), name);
        }
    }
}

void check_pyasukp_corpus() {
    const std::filesystem::path data_dir = std::filesystem::path(UKP_SOURCE_DIR) / "data";
    std::size_t files_seen = 0;
    std::size_t dense_checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ukp") continue;
        ++files_seen;
        const Instance instance = read_instance_file(entry.path().string());
        require(!instance.items.empty() && instance.capacity > 0,
                entry.path().filename().string() + ": invalid PYAsUKP corpus instance");
        if (instance.capacity <= kDenseCapacityLimit) {
            check_faithful(instance, dense_dp_value(instance), entry.path().filename().string());
            ++dense_checked;
        }
    }
    require(files_seen > 0, "PYAsUKP corpus was not found");
    require(dense_checked > 0, "no PYAsUKP corpus instance was eligible for dense oracle");
}

}  // namespace

int main() {
    try {
        check_article_examples();
        check_generated_families();
        check_pyasukp_corpus();
        check_faithful_switches();
        std::cout << "faithful correctness suite passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
