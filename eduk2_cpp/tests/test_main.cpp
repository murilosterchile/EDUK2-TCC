#include "ukp/bounds.hpp"
#include "ukp/dominance.hpp"
#include "ukp/faithful_solver.hpp"
#include "ukp/generator.hpp"
#include "ukp/io.hpp"
#include "ukp/verify.hpp"
#include "../src/faithful/preprocessing.hpp"
#include "../src/faithful/critical_sequence.hpp"

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

void check_skip_point_sequence() {
    // Includes gaps, collisions at a weight, equal-profit collisions, a
    // weight-one item, and deliberately unordered IDs/weights.
    const std::vector<Item> items{{17, 4, 7}, {3, 1, 2}, {29, 3, 6},
                                  {8, 2, 4}, {41, 5, 9}, {5, 3, 6}};
    constexpr Weight limit = 31;
    std::vector<Profit> dense(static_cast<std::size_t>(limit + 1), 0);
    for (Weight y = 1; y <= limit; ++y) {
        for (const Item& item : items) {
            if (item.w <= y) dense[static_cast<std::size_t>(y)] = std::max(
                dense[static_cast<std::size_t>(y)],
                safe_add(dense[static_cast<std::size_t>(y - item.w)], item.p));
        }
    }

    for (const Weight height : {Weight{1}, Weight{4}, Weight{7}}) {
        faithful::detail::CriticalSequence sequence;
        for (Weight ya = 0; ya < limit; ya += height) {
            const Weight yb = std::min(limit, ya + height);
            sequence.process_slice(ya, yb, limit, items,
                                   [](faithful::detail::PointId) { return true; });
        }
        for (Weight y = 0; y <= limit; ++y) {
            require(sequence.value_at(y) == dense[static_cast<std::size_t>(y)],
                    "skip-point value differs from dense DP");
        }

        const auto& points = sequence.skip_points();
        require(!points.empty(), "skip-point sequence is empty");
        const auto& root = sequence.state(points.front());
        require(root.weight == 0 && root.profit == 0 && root.predecessor == faithful::detail::no_point,
                "skip-point root is invalid");
        for (std::size_t i = 1; i < points.size(); ++i) {
            const auto& prior = sequence.state(points[i - 1]);
            const auto& state = sequence.state(points[i]);
            require(state.weight > prior.weight && state.profit > prior.profit,
                    "skip-points are not strictly ordered");
            require(state.predecessor != faithful::detail::no_point && state.predecessor < points[i],
                    "skip-point predecessor is not preserved");
            const auto item = std::find_if(items.begin(), items.end(), [&](const Item& x) {
                return x.id == state.item_id;
            });
            require(item != items.end(), "skip-point item is missing");
            const auto& parent = sequence.state(state.predecessor);
            require(parent.weight + item->w == state.weight && parent.profit + item->p == state.profit,
                    "skip-point transition is inconsistent");
        }
        const auto chosen = points.back();
        Weight reconstructed_weight = 0;
        Profit reconstructed_profit = 0;
        for (auto cursor = chosen; cursor != faithful::detail::no_point;
             cursor = sequence.state(cursor).predecessor) {
            const auto& state = sequence.state(cursor);
            if (state.item_id < 0) break;
            const auto item = std::find_if(items.begin(), items.end(), [&](const Item& x) {
                return x.id == state.item_id;
            });
            reconstructed_weight += item->w;
            reconstructed_profit += item->p;
        }
        require(reconstructed_weight == sequence.state(chosen).weight &&
                    reconstructed_profit == sequence.state(chosen).profit,
                "skip-point reconstruction is invalid");
    }
}

void check_equal_profit_predecessor_order() {
    // The first generated candidate at a weight must win an equal-profit tie;
    // reconstruction depends on retaining that deterministic predecessor.
    const std::vector<Item> items{{41, 2, 3}, {99, 2, 3}};
    faithful::detail::CriticalSequence sequence;
    sequence.process_slice(0, 2, 2, items,
                           [](faithful::detail::PointId) { return true; });
    const auto chosen = sequence.state(sequence.state_at_or_before(2));
    require(chosen.weight == 2 && chosen.profit == 3,
            "equal-profit candidate was not retained");
    require(chosen.predecessor == 0 && chosen.item_id == 41,
            "equal-profit tie did not retain the first predecessor");
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

void check_faithful_core_selection() {
    // The best item is selected from the original working instance, before
    // optional simple dominance and before any ratio ordering.
    const Instance best_first{300, {{7, 30, 270}, {3, 10, 100}, {8, 20, 190}}};
    const auto preprocessing = faithful::detail::preprocess_items(best_first, true);
    require(preprocessing.best_item.id == 3,
            "preprocessing did not select the best item directly from the instance");
    require(preprocessing.multiple_removed == 2,
            "multiple dominance did not use the best original-instance item");
    require(std::none_of(preprocessing.items.begin(), preprocessing.items.end(),
                         [](const Item& item) { return item.id == 7; }),
            "item dominated by the original best item survived preprocessing");

    Instance instance;
    instance.capacity = 2'500;
    instance.items.push_back({0, 100, 10'000});
    // All survivors have distinct ratios below the best item's ratio.  The
    // item at weight 200 is deliberately globally multiple-dominated.
    for (int weight = 1; weight <= 120; ++weight) {
        if (weight == 100) continue;
        instance.items.push_back({static_cast<int>(instance.items.size()), weight, 100 * weight - 1});
    }
    const int dominated_id = static_cast<int>(instance.items.size());
    instance.items.push_back({dominated_id, 200, 19'999});

    const auto reduced = faithful::detail::preprocess_items(instance, false);
    require(reduced.multiple_removed == 1, "global multiple dominance was not recorded");
    require(std::none_of(reduced.items.begin(), reduced.items.end(), [dominated_id](const Item& item) {
                return item.id == dominated_id;
            }), "multiple-dominated item reached the reduced global list");
    require(reduced.items.size() > 100, "test instance has too few global survivors");
    require(std::is_sorted(reduced.items.begin(), reduced.items.end(), better_ratio),
            "reduced global list is not ratio ordered");

    SolverOptions faithful_options;
    faithful_options.use_bounds = false;
    faithful_options.use_periodicity = false;
    faithful_options.core_size = 3;
    faithful_options.bb_node_limit = 7;
    faithful_options.use_core_remainder_ordering = true;
    faithful_options.use_core_multiple_dominance = true;
    faithful_options.use_modular_dominance = true;
    const SolverResult faithful_result = faithful::Solver(faithful_options).solve(instance);
    const std::size_t n = faithful_result.stats.dp_item_ids.size();
    const std::size_t expected_core = std::min(n, std::max<std::size_t>(100, n / 100));
    require(faithful_result.stats.core_item_ids.size() == expected_core,
            "faithful core did not use prescribed C");
    require(faithful_result.stats.core_node_limit == 10'000,
            "faithful B&B did not use fixed node limit");
    require(faithful_result.stats.items_removed_core_multiple == 0 &&
                faithful_result.stats.items_removed_modular == 0,
            "faithful core applied local reductions");
    for (std::size_t i = 0; i < expected_core; ++i) {
        require(faithful_result.stats.core_item_ids[i] == faithful_result.stats.dp_item_ids[i],
                "faithful core is not the exact ratio-ordered DP prefix");
    }

    SolverOptions without_bb = faithful_options;
    without_bb.use_core_bb = false;
    const SolverResult no_bb_result = faithful::Solver(without_bb).solve(instance);
    require(no_bb_result.stats.dp_item_ids == faithful_result.stats.dp_item_ids,
            "B&B changed the global item list delivered to DP");

    SolverOptions experimental = without_bb;
    experimental.paper_faithful_mode = false;
    experimental.use_core_bb = true;
    experimental.use_core_remainder_ordering = true;
    experimental.use_core_multiple_dominance = true;
    const SolverResult experimental_result = faithful::Solver(experimental).solve(instance);
    require(experimental_result.stats.dp_item_ids == no_bb_result.stats.dp_item_ids,
            "experimental local core processing changed the global DP list");
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

void check_faithful_extended_prefix_regression() {
    // The final two-state aggregation needs the interval after c/2.  This
    // instance regressed when the slice loop stopped at half_capacity rather
    // than extending once through the post-threshold active-item range.
    const auto path = std::filesystem::path(UKP_SOURCE_DIR) / "data" / "exnsdbis10.ukp";
    const Instance instance = read_instance_file(path.string());
    const SolverResult result = faithful::Solver(SolverOptions{}).solve(instance);
    require(verify_solution(instance, result.solution),
            "extended-prefix regression: infeasible faithful solution");
    require(result.solution.profit == 1'028'035,
            "extended-prefix regression: faithful missed the optimum");
    require(result.solution.weight == 894'642,
            "extended-prefix regression: faithful selected the wrong solution");
}

}  // namespace

int main() {
    try {
        check_article_examples();
        check_generated_families();
        check_pyasukp_corpus();
        check_faithful_extended_prefix_regression();
        check_faithful_switches();
        check_faithful_core_selection();
        check_skip_point_sequence();
        check_equal_profit_predecessor_order();
        std::cout << "faithful correctness suite passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
