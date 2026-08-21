#include "ukp/bounds.hpp"
#include "ukp/dominance.hpp"
#include "ukp/faithful_solver.hpp"
#include "ukp/generator.hpp"
#include "ukp/io.hpp"
#include "ukp/verify.hpp"
#include "../src/faithful/critical_sequence.hpp"
#include "../src/faithful/eduk2_bounds.hpp"
#include "../src/faithful/incumbent.hpp"
#include "../src/faithful/preprocessing.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ukp;

namespace {

constexpr Weight kDenseCapacityLimit = 20'000;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Operation>
void require_overflow(Operation&& operation, const std::string& message) {
    try {
        operation();
    } catch (const std::overflow_error&) {
        return;
    }
    throw std::runtime_error(message);
}

void check_safe_arithmetic() {
    constexpr auto maximum = std::numeric_limits<long long>::max();
    constexpr auto minimum = std::numeric_limits<long long>::min();
    require(safe_add(17, 25) == 42 && safe_mul(6, 7) == 42 &&
                floor_mul_div(6, 7, 5) == 8,
            "safe arithmetic changed an in-range result");
    require_overflow([&] { static_cast<void>(safe_add(maximum, 1)); },
                     "safe_add did not detect overflow");
    require_overflow([&] { static_cast<void>(safe_add(minimum, -1)); },
                     "safe_add did not detect underflow");
    require_overflow([&] { static_cast<void>(safe_mul(maximum, 2)); },
                     "safe_mul did not detect overflow");
    require_overflow([&] { static_cast<void>(floor_mul_div(maximum, 2, 2)); },
                     "floor_mul_div changed intermediate-overflow semantics");
}

void check_input_parser_compatibility() {
    std::istringstream pyasukp{
        "# header comment\n"
        " n: +3 # fallback spelling\n"
        "c: 17\n"
        "begin data\n"
        "4 5.4\n"
        "+6 +7.5\n"
        "malformed item\n"
        "3 8.49 ignored trailing fields\n"
        "end data\n"};
    const Instance parsed_pyasukp = read_instance(pyasukp);
    require(parsed_pyasukp.capacity == 17 && parsed_pyasukp.items.size() == 3,
            "fast PYAsUKP parser changed the header");
    require(parsed_pyasukp.items[0].id == 0 && parsed_pyasukp.items[0].w == 4 &&
                parsed_pyasukp.items[0].p == 5 &&
                parsed_pyasukp.items[1].id == 1 && parsed_pyasukp.items[1].w == 6 &&
                parsed_pyasukp.items[1].p == 8 &&
                parsed_pyasukp.items[2].id == 2 && parsed_pyasukp.items[2].w == 3 &&
                parsed_pyasukp.items[2].p == 8,
            "fast PYAsUKP parser changed item parsing or rounding");

    std::istringstream legacy{
        "2 +20 # n and capacity\n"
        "+5 4\n"
        "7 +3 ignored trailing text\n"};
    const Instance parsed_legacy = read_instance(legacy);
    require(parsed_legacy.capacity == 20 && parsed_legacy.items.size() == 2 &&
                parsed_legacy.items[0].id == 0 && parsed_legacy.items[0].p == 5 &&
                parsed_legacy.items[0].w == 4 && parsed_legacy.items[1].id == 1 &&
                parsed_legacy.items[1].p == 7 && parsed_legacy.items[1].w == 3,
            "fast legacy parser changed stream-compatible parsing");
}

void check_sparse_incumbent_updates() {
    faithful::detail::Incumbent incumbent(6);
    std::vector<long long> dense{0, 2, 0, 0, 3, 0};
    require(incumbent.consider(10, 9, dense), "dense incumbent setup was rejected");

    dense[1] = 0;
    dense[4] = 0;
    dense[2] = 5;
    require(incumbent.consider_sparse(11, 10, dense, {2}),
            "sparse incumbent improvement was rejected");
    const Solution solution = incumbent.solution("test");
    require(solution.profit == 11 && solution.weight == 10 &&
                solution.multiplicity_by_id == dense,
            "sparse incumbent retained stale multiplicities");
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
            long long successor_item_scans = 0;
            long long backfill_attempts = 0;
            long long cursor_advances = 0;
            long long historical_states_avoided = 0;
            long long considered_for_introduction = 0;
            long long introduced = 0;
            long long rejected_by_envelope = 0;
            long long rejected_by_bound = 0;
            for (const SliceStats& slice : result.stats.slices) {
                require(slice.begin <= slice.end, prefix + "invalid slice range");
                fathomed += slice.states_fathomed_by_bound;
                threshold += slice.items_removed_threshold;
                successor_item_scans += slice.successor_item_scans;
                backfill_attempts += slice.backfill_attempts;
                cursor_advances += slice.cursor_advances;
                historical_states_avoided += slice.historical_states_avoided;
                considered_for_introduction += slice.items_considered_for_introduction;
                introduced += slice.items_introduced;
                rejected_by_envelope += slice.items_rejected_by_envelope;
                rejected_by_bound += slice.items_rejected_by_bound;
            }
            require(fathomed == result.stats.states_fathomed, prefix + "slice fathoming mismatch");
            require(threshold == result.stats.items_removed_threshold, prefix + "slice threshold mismatch");
            require(successor_item_scans == result.stats.successor_item_scans,
                    prefix + "slice item-scan mismatch");
            require(backfill_attempts == result.stats.backfill_attempts,
                    prefix + "slice backfill mismatch");
            require(cursor_advances == result.stats.cursor_advances,
                    prefix + "slice cursor-advance mismatch");
            require(historical_states_avoided == result.stats.historical_states_avoided,
                    prefix + "slice avoided-history mismatch");
            require(considered_for_introduction == result.stats.items_considered_for_introduction &&
                        introduced == result.stats.items_introduced &&
                        rejected_by_envelope == result.stats.items_rejected_by_envelope &&
                        rejected_by_bound == result.stats.items_rejected_by_bound,
                    prefix + "slice introduction mismatch");
            require(considered_for_introduction == introduced + rejected_by_envelope + rejected_by_bound,
                    prefix + "introduction decisions do not partition the candidates");
            require(result.stats.slices.back().active_items_after == result.stats.active_items_final,
                    prefix + "final active-item mismatch");
            require(result.stats.states_scanned ==
                        result.stats.states_kept + result.stats.states_fathomed,
                    prefix + "state decisions do not partition scanned states");
            require(result.stats.states_expanded == result.stats.states_kept,
                    prefix + "expanded-state count differs from surviving states");
            require(result.stats.active_item_samples == result.stats.states_expanded,
                    prefix + "active-item sampling does not match expansions");
            require(result.stats.successor_attempts == result.stats.points_generated,
                    prefix + "successor attempts differ from generated points");
            require(result.stats.cursor_advances == result.stats.successor_attempts,
                    prefix + "cursor advances differ from incremental successors");
            require(result.stats.successor_attempts ==
                        result.stats.candidates_stored +
                            result.stats.computed_window_rejections,
                    prefix + "window decisions do not partition successor attempts");
            require(result.stats.computed_window_collisions ==
                        result.stats.computed_window_replacements +
                            result.stats.computed_window_rejections,
                    prefix + "window collision decisions do not partition collisions");
            require(result.stats.computed_window_index_collisions == 0,
                    prefix + "live computed-window entries aliased");
            const long long introduced_by_capacity = std::accumulate(
                result.stats.items_introduced_by_capacity_decile.begin(),
                result.stats.items_introduced_by_capacity_decile.end(), 0LL);
            require(introduced_by_capacity == result.stats.items_introduced,
                    prefix + "introduction capacity histogram mismatch");
            const long long introduced_by_reduction = std::accumulate(
                result.stats.items_introduced_by_reduction_decile.begin(),
                result.stats.items_introduced_by_reduction_decile.end(), 0LL);
            require(introduced_by_reduction == result.stats.items_introduced,
                    prefix + "introduction reduction-range histogram mismatch");
            long long per_item_backfills = 0;
            long long per_item_entries = 0;
            for (const long long attempts : result.stats.backfill_attempts_by_item) {
                if (attempts < 0) continue;
                per_item_backfills += attempts;
                ++per_item_entries;
            }
            require(per_item_entries == result.stats.items_introduced,
                    prefix + "per-item backfill entries differ from introductions");
            require(per_item_backfills == result.stats.backfill_attempts,
                    prefix + "per-item backfills differ from aggregate");
            long long contextual_wins = 0;
            for (const auto& [_, count] : result.stats.contextual_bound_wins) {
                contextual_wins += count;
            }
            require(contextual_wins == result.stats.contextual_bound_state_queries +
                        result.stats.contextual_bound_item_queries,
                    prefix + "contextual bound query/winner mismatch");
            long long contextual_state_wins = 0;
            for (const auto& [_, count] : result.stats.contextual_bound_state_wins) {
                contextual_state_wins += count;
            }
            long long contextual_item_wins = 0;
            for (const auto& [_, count] : result.stats.contextual_bound_item_wins) {
                contextual_item_wins += count;
            }
            require(contextual_state_wins == result.stats.contextual_bound_state_queries &&
                        contextual_item_wins == result.stats.contextual_bound_item_queries &&
                        contextual_state_wins + contextual_item_wins == contextual_wins,
                    prefix + "contextual query kinds do not partition winners");
            long long contextual_fathoms = 0;
            for (const auto& [_, count] : result.stats.contextual_bound_fathoms) {
                contextual_fathoms += count;
            }
            require(contextual_fathoms == result.stats.states_fathomed,
                    prefix + "contextual fathom attribution mismatch");
            require(result.stats.contextual_bound_calls_avoided_by_lower ==
                        result.stats.contextual_bound_state_calls_avoided_by_lower +
                            result.stats.contextual_bound_item_calls_avoided_by_lower,
                    prefix + "lower-bound avoidance mismatch");
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

        const auto& states = sequence.states();
        require(!states.empty(), "skip-point sequence is empty");
        const auto& root = states.front();
        require(root.weight == 0 && root.profit == 0 &&
                    root.predecessor == faithful::detail::no_point,
                "skip-point root is invalid");
        for (std::size_t i = 1; i < states.size(); ++i) {
            const auto& prior = states[i - 1];
            const auto& state = states[i];
            require(state.weight > prior.weight && state.profit > prior.profit,
                    "skip-points are not strictly ordered");
            require(state.predecessor != faithful::detail::no_point && state.predecessor < i,
                    "skip-point predecessor is not preserved");
            const auto item = std::find_if(items.begin(), items.end(), [&](const Item& x) {
                return x.id == state.item_id;
            });
            require(item != items.end(), "skip-point item is missing");
            const auto& parent = sequence.state(state.predecessor);
            require(parent.weight + item->w == state.weight && parent.profit + item->p == state.profit,
                    "skip-point transition is inconsistent");
        }
        const faithful::detail::PointId chosen = states.size() - 1;
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

void check_incremental_item_introduction() {
    const std::vector<Item> all_items{{10, 2, 3}, {11, 3, 5}, {12, 4, 6},
                                      {13, 5, 9}, {14, 6, 10}};
    std::vector<Item> ratio_order = all_items;
    std::sort(ratio_order.begin(), ratio_order.end(), better_ratio);
    std::vector<faithful::detail::ActiveItem> weight_order;
    for (std::size_t rank = 0; rank < ratio_order.size(); ++rank) {
        const Item& item = ratio_order[rank];
        weight_order.push_back({item.id, static_cast<int>(rank), item.w, item.p});
    }
    std::stable_sort(weight_order.begin(), weight_order.end(),
                     [](const faithful::detail::ActiveItem& left,
                        const faithful::detail::ActiveItem& right) {
        if (left.w != right.w) return left.w < right.w;
        return left.tie_rank < right.tie_rank;
    });

    faithful::detail::CriticalSequence sequence;
    sequence.configure_item_order(ratio_order);
    std::vector<faithful::detail::ActiveItem> active;
    std::size_t next_item = 0;
    faithful::detail::SliceBuildResult total;
    constexpr Weight limit = 30;
    for (Weight ya = 0; ya < limit; ya += 4) {
        const auto part = sequence.process_slice_incremental(
            ya, std::min(limit, ya + 4), limit, active, weight_order, next_item,
            [](const faithful::detail::ActiveItem&, Profit) { return true; },
            [](faithful::detail::PointId) { return true; });
        total.successor_attempts += part.successor_attempts;
        total.backfill_attempts += part.backfill_attempts;
        total.cursor_advances += part.cursor_advances;
        total.items_considered_for_introduction += part.items_considered_for_introduction;
        total.items_introduced += part.items_introduced;
        total.items_rejected_by_envelope += part.items_rejected_by_envelope;
        total.items_rejected_by_bound += part.items_rejected_by_bound;
    }

    require(next_item == weight_order.size() && total.items_considered_for_introduction == 5,
            "incremental selector did not visit every item");
    require(total.items_introduced == 3 && total.items_rejected_by_envelope == 2 &&
                total.items_rejected_by_bound == 0,
            "incremental envelope accepted the wrong item set");
    require(total.backfill_attempts > 0 && total.successor_attempts >= total.backfill_attempts,
            "incremental introduction did not backfill prior states");
    require(total.cursor_advances == total.successor_attempts,
            "incremental cursors did not account for every successor");
    require(active.size() == 3 && active[0].id == 13 && active[1].id == 11 && active[2].id == 10,
            "introduced items did not retain decreasing ratio order");

    const Instance dense_instance{limit, all_items};
    for (Weight capacity = 0; capacity <= limit; ++capacity) {
        Instance prefix = dense_instance;
        prefix.capacity = capacity;
        require(sequence.value_at(capacity) == dense_dp_value(prefix),
                "incremental sequence differs from dense DP");
    }

    // At weight five, A+B can be generated with either last item.  The later
    // introduced, higher-ratio B must win independently of generation order.
    const std::vector<Item> tie_ratio_order{{11, 3, 5}, {10, 2, 3}};
    std::vector<faithful::detail::ActiveItem> tie_weight_order{
        {10, 1, 2, 3}, {11, 0, 3, 5}};
    faithful::detail::CriticalSequence tie_sequence;
    tie_sequence.configure_item_order(tie_ratio_order);
    std::vector<faithful::detail::ActiveItem> tie_active;
    std::size_t tie_next = 0;
    tie_sequence.process_slice_incremental(
        0, 5, 5, tie_active, tie_weight_order, tie_next,
        [](const faithful::detail::ActiveItem&, Profit) { return true; },
        [](faithful::detail::PointId) { return true; });
    const auto& tied = tie_sequence.state(tie_sequence.state_at_or_before(5));
    require(tied.weight == 5 && tied.profit == 8 && tied.item_id == 11 &&
                tie_sequence.state(tied.predecessor).weight == 2,
            "incremental equal-profit tie did not retain ratio priority");
}

struct GreedyCompletion {
    Weight used_weight = 0;
    Profit profit = 0;
    std::vector<long long> multiplicity;
};

GreedyCompletion original_greedy_completion(Weight capacity, Weight initial_weight,
                                            Profit initial_profit, const std::vector<Item>& items) {
    GreedyCompletion result{initial_weight, initial_profit, std::vector<long long>(items.size(), 0)};
    for (const Item& item : items) {
        const long long copies = (capacity - result.used_weight) / item.w;
        if (copies == 0) continue;
        result.used_weight += safe_mul(copies, item.w);
        result.profit = safe_add(result.profit, safe_mul(copies, item.p));
        result.multiplicity[static_cast<std::size_t>(item.id)] += copies;
    }
    return result;
}

GreedyCompletion optimized_greedy_completion(Weight capacity, Weight initial_weight,
                                             Profit initial_profit, const std::vector<Item>& items) {
    std::vector<Weight> suffix_minimum(items.size());
    Weight minimum_weight = items.back().w;
    for (std::size_t index = items.size(); index-- > 0;) {
        minimum_weight = std::min(minimum_weight, items[index].w);
        suffix_minimum[index] = minimum_weight;
    }

    GreedyCompletion result{initial_weight, initial_profit, std::vector<long long>(items.size(), 0)};
    Weight remaining = capacity - result.used_weight;
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (remaining < suffix_minimum[index]) break;
        const Item& item = items[index];
        if (item.w > remaining) continue;
        const long long copies = remaining / item.w;
        const Weight added_weight = safe_mul(copies, item.w);
        result.used_weight += added_weight;
        remaining -= added_weight;
        result.profit = safe_add(result.profit, safe_mul(copies, item.p));
        result.multiplicity[static_cast<std::size_t>(item.id)] += copies;
    }
    return result;
}

void check_greedy_completion_equivalence() {
    const std::vector<std::vector<Item>> item_sets{
        // Only the late item fits initially; earlier oversized items must be skipped.
        {{0, 17, 31}, {1, 23, 47}, {2, 5, 9}, {3, 19, 38}},
        {{0, 8, 15}, {1, 3, 5}, {2, 13, 26}, {3, 2, 3}, {4, 11, 20}},
        {{0, 9, 19}, {1, 14, 30}, {2, 6, 11}, {3, 4, 7}},
    };
    for (const auto& items : item_sets) {
        for (const Weight capacity : {Weight{5}, Weight{11}, Weight{18}, Weight{37}, Weight{64}}) {
            for (const Weight initial_weight : {Weight{0}, Weight{1}, capacity / 2, capacity}) {
                const GreedyCompletion original =
                    original_greedy_completion(capacity, initial_weight, 41, items);
                const GreedyCompletion optimized =
                    optimized_greedy_completion(capacity, initial_weight, 41, items);
                require(optimized.used_weight == original.used_weight &&
                            optimized.profit == original.profit &&
                            optimized.multiplicity == original.multiplicity,
                        "optimized greedy completion differs from original traversal");
            }
        }
    }
}

void check_faithful_unordered_valid_ids() {
    // Item IDs are valid multiplicity indexes but deliberately do not follow
    // input order.  This exercises final-trace reconstruction through the
    // direct ID-to-weight lookup.
    const Instance instance{63, {{2, 15, 17}, {0, 20, 30}, {1, 25, 40}}};
    check_faithful(instance, dense_dp_value(instance), "unordered valid IDs");
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

struct DirectRational {
    Profit numerator = 0;
    Weight denominator = 1;
};

bool direct_greater(const DirectRational& left, const DirectRational& right) {
    return static_cast<__int128>(left.numerator) * right.denominator >
           static_cast<__int128>(right.numerator) * left.denominator;
}

// Keep a local copy of the original q* scan as a regression oracle for the
// values cached by BoundContext.
DirectRational direct_q_star(const std::vector<Item>& items, const Item& base) {
    DirectRational best{0, 1};
    for (const Item& item : items) {
        if (item.id == base.id) continue;
        const Weight copies = item.w / base.w;
        const Weight remainder = item.w - copies * base.w;
        const __int128 numerator = static_cast<__int128>(item.p) -
                                   static_cast<__int128>(copies) * base.p;
        if (remainder <= 0 || numerator <= 0) continue;
        const DirectRational candidate{static_cast<Profit>(numerator), remainder};
        if (direct_greater(candidate, best)) best = candidate;
    }
    return best;
}

BoundValue direct_normalized_bound(const Item& normalized_base, const Item& original_base,
                                   Weight capacity, DirectRational q, int psi, BoundType type) {
    const Weight copies = capacity / normalized_base.w;
    const __int128 normalized_upper = static_cast<__int128>(q.numerator) * capacity +
        (static_cast<__int128>(normalized_base.p) * q.denominator -
         static_cast<__int128>(q.numerator) * normalized_base.w) * copies;
    return {static_cast<Profit>(normalized_upper /
                                (static_cast<__int128>(q.denominator) * psi)),
            safe_mul(copies, original_base.p), type};
}

void check_precomputed_q_star() {
    const std::vector<std::vector<Item>> item_sets{
        {{10, 9, 12}, {3, 4, 5}, {17, 14, 19}, {2, 7, 9}, {21, 17, 22}},
        {{5, 5, 4}, {8, 7, 5}, {1, 11, 7}, {13, 13, 8}, {29, 17, 10}},
        {{4, 3, 8}, {6, 5, 12}, {9, 8, 19}, {12, 13, 30}, {15, 21, 47}}};

    for (const auto& items : item_sets) {
        const BoundContext context = make_bound_context(items);
        const DirectRational direct_tau = direct_q_star(
            context.normalized_ratio_items, context.normalized_tau_star_base);
        const DirectRational direct_best = direct_q_star(
            context.normalized_ratio_items, context.normalized_best_item_star_base);
        require(context.tau_star_q_star_num == direct_tau.numerator &&
                    context.tau_star_q_star_den == direct_tau.denominator,
                "cached tau q* differs from direct scan");
        require(context.best_item_star_q_star_num == direct_best.numerator &&
                    context.best_item_star_q_star_den == direct_best.denominator,
                "cached best-item q* differs from direct scan");

        for (const Weight capacity : {Weight{0}, Weight{1}, Weight{4}, Weight{17}, Weight{68}}) {
            DirectRational tau_q = direct_tau;
            if (direct_greater(tau_q, {1, 1})) tau_q = {1, 1};
            const BoundValue expected_tau = direct_normalized_bound(
                context.normalized_tau_star_base, context.tau_star_base, capacity, tau_q,
                context.psi, BoundType::TauStar);
            const BoundValue expected_best = direct_normalized_bound(
                context.normalized_best_item_star_base, context.best_item_star_base, capacity,
                direct_best, context.psi, BoundType::BestItemStar);
            const BoundValue actual_tau = compute_tau_star(context, capacity);
            const BoundValue actual_best = compute_best_item_star(context, capacity);
            require(actual_tau.upper == expected_tau.upper && actual_tau.lower == expected_tau.lower &&
                        actual_tau.type == expected_tau.type,
                    "cached tau q* changed a bound result");
            require(actual_best.upper == expected_best.upper && actual_best.lower == expected_best.lower &&
                        actual_best.type == expected_best.type,
                    "cached best-item q* changed a bound result");
        }
    }
}

bool same_item(const Item& left, const Item& right) {
    return left.id == right.id && left.w == right.w && left.p == right.p;
}

bool same_items(const std::vector<Item>& left, const std::vector<Item>& right) {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), same_item);
}

void check_ratio_ordered_context_rebuild() {
    std::vector<Item> ordered{{9, 8, 17}, {4, 3, 6}, {7, 11, 21},
                              {1, 5, 9}, {3, 13, 22}, {6, 2, 3}};
    std::sort(ordered.begin(), ordered.end(), better_ratio);
    BoundContext persistent;
    std::vector<Item> residual = ordered;

    for (std::size_t step = 0; !residual.empty(); ++step) {
        const BoundContext expected = make_bound_context(residual);
        rebuild_bound_context_ratio_ordered(persistent, residual);
        require(same_items(persistent.items, expected.items) &&
                    same_items(persistent.normalized_ratio_items,
                               expected.normalized_ratio_items) &&
                    persistent.psi == expected.psi &&
                    persistent.tau_star_q_star_num == expected.tau_star_q_star_num &&
                    persistent.tau_star_q_star_den == expected.tau_star_q_star_den &&
                    persistent.tau_star_q_star_item_id == expected.tau_star_q_star_item_id &&
                    persistent.best_item_star_q_star_num ==
                        expected.best_item_star_q_star_num &&
                    persistent.best_item_star_q_star_den ==
                        expected.best_item_star_q_star_den &&
                    persistent.best_item_star_q_star_item_id ==
                        expected.best_item_star_q_star_item_id &&
                    persistent.alpha_num == expected.alpha_num &&
                    persistent.alpha_den == expected.alpha_den &&
                    persistent.alpha_item_id == expected.alpha_item_id &&
                    persistent.no_multiple_dominance == expected.no_multiple_dominance &&
                    persistent.multiple_dominance_dominator_id ==
                        expected.multiple_dominance_dominator_id &&
                    persistent.multiple_dominance_dominated_id ==
                        expected.multiple_dominance_dominated_id &&
                    persistent.certified_type_count == expected.certified_type_count &&
                    std::equal(persistent.certified_types.begin(),
                               persistent.certified_types.begin() +
                                   persistent.certified_type_count,
                               expected.certified_types.begin()),
                "persistent ratio-ordered context changed cached fields");
        for (const Weight capacity : {Weight{0}, Weight{1}, Weight{17}, Weight{61}}) {
            for (const BoundPolicy policy : {BoundPolicy::U3, BoundPolicy::V,
                                              BoundPolicy::TauStar,
                                              BoundPolicy::BestItemStar,
                                              BoundPolicy::BestCertified}) {
                const BoundValue actual = compute_bound(persistent, capacity, policy);
                const BoundValue oracle = compute_bound(expected, capacity, policy);
                require(actual.upper == oracle.upper && actual.lower == oracle.lower &&
                            actual.type == oracle.type,
                        "persistent ratio-ordered context changed a bound");
            }
        }

        if (residual.size() == 1) break;
        const std::array<int, 3> cached_witnesses{
            persistent.tau_star_q_star_item_id,
            persistent.best_item_star_q_star_item_id,
            persistent.alpha_item_id};
        const int witness = cached_witnesses[step % cached_witnesses.size()];
        auto removal = std::find_if(residual.begin(), residual.end(), [&](const Item& item) {
            return item.id == witness;
        });
        if (removal == residual.end()) {
            removal = residual.begin() + static_cast<std::ptrdiff_t>(residual.size() / 2);
        }
        residual.erase(removal);
    }
}

void check_multiple_dominance_witness_persistence() {
    std::vector<Item> residual{{10, 2, 4}, {20, 5, 9}, {30, 6, 8}};
    std::sort(residual.begin(), residual.end(), better_ratio);
    BoundContext context;
    BoundContextTelemetry telemetry;

    rebuild_bound_context_ratio_ordered(context, residual, &telemetry);
    require(!context.no_multiple_dominance &&
                context.multiple_dominance_dominator_id == 10 &&
                context.multiple_dominance_dominated_id == 30,
            "multiple-dominance search did not retain its witness IDs");
    require(telemetry.dominance_full_searches == 1 &&
                telemetry.dominance_searches_avoided_by_witness == 0 &&
                telemetry.dominance_witness_invalidations == 0 &&
                telemetry.dominance_pair_checks == 3,
            "initial multiple-dominance telemetry is inconsistent");

    rebuild_bound_context_ratio_ordered(context, residual, &telemetry);
    require(!context.no_multiple_dominance &&
                context.multiple_dominance_dominator_id == 10 &&
                context.multiple_dominance_dominated_id == 30 &&
                telemetry.dominance_full_searches == 1 &&
                telemetry.dominance_searches_avoided_by_witness == 1 &&
                telemetry.dominance_witness_invalidations == 0 &&
                telemetry.dominance_pair_checks == 3,
            "live multiple-dominance witness was not reused");

    residual.erase(std::find_if(residual.begin(), residual.end(),
        [](const Item& item) { return item.id == 10; }));
    rebuild_bound_context_ratio_ordered(context, residual, &telemetry);
    require(!context.no_multiple_dominance &&
                context.multiple_dominance_dominator_id == 20 &&
                context.multiple_dominance_dominated_id == 30 &&
                telemetry.dominance_full_searches == 2 &&
                telemetry.dominance_searches_avoided_by_witness == 1 &&
                telemetry.dominance_witness_invalidations == 1 &&
                telemetry.dominance_pair_checks == 5,
            "invalidated multiple-dominance witness was not replaced");

    residual.erase(std::find_if(residual.begin(), residual.end(),
        [](const Item& item) { return item.id == 30; }));
    rebuild_bound_context_ratio_ordered(context, residual, &telemetry);
    require(context.no_multiple_dominance &&
                context.multiple_dominance_dominator_id == -1 &&
                context.multiple_dominance_dominated_id == -1 &&
                telemetry.dominance_full_searches == 3 &&
                telemetry.dominance_searches_avoided_by_witness == 1 &&
                telemetry.dominance_witness_invalidations == 2 &&
                telemetry.dominance_pair_checks == 6,
            "last removed multiple-dominance witness was not invalidated");

    rebuild_bound_context_ratio_ordered(context, residual, &telemetry);
    require(context.no_multiple_dominance &&
                telemetry.dominance_full_searches == 3 &&
                telemetry.dominance_searches_avoided_by_witness == 1 &&
                telemetry.dominance_witness_invalidations == 2 &&
                telemetry.dominance_pair_checks == 6,
            "dominance-free shrinking context repeated the full search");
}

// Regression oracle for the pre-optimization selection: it intentionally
// performs the old full ratio sort, while production selects the same prefix
// in one pass.
void check_linear_ratio_selection_equivalence() {
    std::vector<std::vector<Item>> cases{
        {{9, 6, 12}, {2, 3, 6}, {7, 9, 18}, {1, 4, 8}, {4, 12, 24}},
        // Equal ratios exercise better_ratio's weight and id tie breakers.
        {{9, 8, 16}, {4, 2, 4}, {7, 4, 8}, {1, 3, 6}, {3, 6, 12}},
        {{5, 7, 13}, {1, 2, 5}},
        {{3, 5, 11}}};
    for (unsigned seed = 1; seed <= 64; ++seed) {
        std::vector<Item> items;
        for (int i = 0; i < 3 + static_cast<int>(seed % 12); ++i) {
            const Weight weight = 1 + ((i * 7 + static_cast<int>(seed)) % 17);
            // The small multiplier deliberately creates many equal ratios.
            const Profit profit = weight * (1 + static_cast<int>((seed + i) % 4));
            items.push_back({i, weight, profit});
        }
        cases.push_back(std::move(items));
    }

    for (const auto& items : cases) {
        std::vector<Item> legacy_ratio_items = items;
        std::sort(legacy_ratio_items.begin(), legacy_ratio_items.end(), better_ratio);
        const BoundContext context = make_bound_context(items);
        require(same_items(context.items, items), "linear context changed residual items");
        require(same_item(context.best, legacy_ratio_items[0]),
                "linear selection changed the best ratio item");
        if (items.size() >= 2)
            require(same_item(context.second, legacy_ratio_items[1]),
                    "linear selection changed the second ratio item");
        if (items.size() >= 3)
            require(context.has_three && same_item(context.third, legacy_ratio_items[2]),
                    "linear selection changed the third ratio item");
        else
            require(!context.has_three, "linear selection changed has_three");

        std::vector<Item> normalized = items;
        for (Item& item : normalized) item.p *= context.psi;
        std::sort(normalized.begin(), normalized.end(), better_ratio);
        require(same_items(context.normalized_ratio_items, normalized),
                "linear selection changed normalized ratio items");
        require(same_item(context.best_item_star_base, legacy_ratio_items[0]),
                "linear selection changed BestItemStar base");

        const Item* expected_lightest = nullptr;
        for (const Item& item : items) {
            if (item.p <= item.w) continue;
            if (!expected_lightest || item.w < expected_lightest->w ||
                (item.w == expected_lightest->w && item.id < expected_lightest->id))
                expected_lightest = &item;
        }
        const Item& expected_tau = expected_lightest ? *expected_lightest : legacy_ratio_items[0];
        const int expected_psi = expected_tau.p <= expected_tau.w
            ? static_cast<int>(expected_tau.w / expected_tau.p + 1) : 1;
        const Profit expected_delta1 = expected_psi * expected_tau.p - expected_tau.w;
        require(context.has_lightest_positive && same_item(context.lightest_positive, expected_tau) &&
                    same_item(context.tau_star_base, expected_tau) &&
                    context.psi == expected_psi && context.delta1 == expected_delta1 &&
                    context.tau_normalized == (expected_psi != 1),
                "linear selection changed tau context fields");
        require(same_item(context.normalized_tau_star_base,
                          {expected_tau.id, expected_tau.w, expected_tau.p * expected_psi}) &&
                    same_item(context.normalized_best_item_star_base,
                              {legacy_ratio_items[0].id, legacy_ratio_items[0].w,
                               legacy_ratio_items[0].p * expected_psi}),
                "linear selection changed normalized base fields");

        Profit expected_alpha_num = 0;
        Weight expected_alpha_den = 1;
        for (const Item& item : normalized) {
            if (item.id == expected_tau.id || item.w < expected_tau.w) continue;
            const Profit delta = item.p - item.w;
            const Weight copies = item.w / expected_tau.w;
            if (copies <= 0) continue;
            const Weight denominator = copies * expected_delta1;
            if (static_cast<__int128>(delta) * expected_alpha_den >
                static_cast<__int128>(expected_alpha_num) * denominator) {
                expected_alpha_num = delta;
                expected_alpha_den = denominator;
            }
        }
        bool expected_no_multiple_dominance = true;
        for (std::size_t i = 0; i < items.size(); ++i) for (std::size_t j = 0; j < items.size(); ++j) {
            if (i == j || items[i].w > items[j].w) continue;
            const Weight copies = items[j].w / items[i].w;
            if (copies > 0 && static_cast<__int128>(copies) * items[i].p >= items[j].p)
                expected_no_multiple_dominance = false;
        }
        require(context.alpha_num == expected_alpha_num && context.alpha_den == expected_alpha_den &&
                    context.preferred == (expected_alpha_num <= expected_alpha_den ? BoundType::V : BoundType::Both) &&
                    context.no_multiple_dominance == expected_no_multiple_dominance,
                "linear selection changed alpha or certification fields");

        const DirectRational direct_tau = direct_q_star(
            context.normalized_ratio_items, context.normalized_tau_star_base);
        const DirectRational direct_best = direct_q_star(
            context.normalized_ratio_items, context.normalized_best_item_star_base);
        require(context.tau_star_q_star_num == direct_tau.numerator &&
                    context.tau_star_q_star_den == direct_tau.denominator &&
                    context.best_item_star_q_star_num == direct_best.numerator &&
                    context.best_item_star_q_star_den == direct_best.denominator,
                "linear selection changed cached q* fields");
        const std::array<BoundType, 3> expected_certified{
            BoundType::U3, BoundType::V, BoundType::BestItemStar};
        const std::size_t expected_certified_count = expected_no_multiple_dominance ? 3 : 2;
        require(context.certified_type_count == expected_certified_count &&
                    std::equal(context.certified_types.begin(),
                               context.certified_types.begin() + expected_certified_count,
                               expected_certified.begin()),
                "linear selection changed certified-bound cache");

        const Weight max_capacity = std::min<Weight>(40, 3 * (*std::max_element(
            items.begin(), items.end(), [](const Item& left, const Item& right) {
                return left.w < right.w;
            })).w);
        for (Weight capacity = 0; capacity <= max_capacity; ++capacity) {
            const Profit oracle = dense_dp_value({capacity, items});
            for (const BoundPolicy policy : {BoundPolicy::U3, BoundPolicy::V, BoundPolicy::TauStar,
                                              BoundPolicy::BestItemStar, BoundPolicy::BestCertified}) {
                const BoundValue value = compute_bound(context, capacity, policy);
                require(value.upper >= oracle && value.lower <= oracle,
                        "linear selection changed a bound policy result");
            }
        }
    }
}

std::vector<BoundType> original_certified_bound_types(const BoundContext& context) {
    std::vector<BoundType> types;
    for (const BoundType type : {BoundType::U3, BoundType::V, BoundType::TauStar,
                                 BoundType::BestItemStar}) {
        if (is_bound_certified(context, type)) types.push_back(type);
    }
    return types;
}

BoundValue original_individual_bound(const BoundContext& context, Weight capacity, BoundType type) {
    switch (type) {
        case BoundType::U3: return compute_u3(context, capacity);
        case BoundType::V: return compute_v(context, capacity);
        case BoundType::TauStar: return compute_tau_star(context, capacity);
        case BoundType::BestItemStar: return compute_best_item_star(context, capacity);
        case BoundType::Both: break;
    }
    throw std::runtime_error("invalid reference certified bound");
}

BoundValue original_best_certified(const BoundContext& context, Weight capacity) {
    const std::vector<BoundType> types = original_certified_bound_types(context);
    require(!types.empty(), "reference certified-bound list is empty");
    BoundValue best = original_individual_bound(context, capacity, types.front());
    for (std::size_t i = 1; i < types.size(); ++i) {
        const BoundValue candidate = original_individual_bound(context, capacity, types[i]);
        if (candidate.upper < best.upper) best = candidate;
    }
    return best;
}

void check_certified_bound_cache_and_policies() {
    const std::vector<std::vector<Item>> item_sets{
        {{0, 3, 4}, {1, 4, 5}, {2, 7, 10}},
        {{0, 2, 3}, {1, 5, 9}, {2, 8, 14}, {3, 11, 18}},
        {{0, 5, 5}, {1, 6, 13}, {2, 9, 18}, {3, 13, 25}}};
    constexpr std::array<BoundType, 4> expected_order{
        BoundType::U3, BoundType::V, BoundType::TauStar, BoundType::BestItemStar};

    for (const auto& items : item_sets) {
        const BoundContext context = make_bound_context(items);
        const std::vector<BoundType> before = original_certified_bound_types(context);
        const std::vector<BoundType> after = certified_bound_types(context);
        require(before == after, "certified-bound API changed its set or order");
        require(context.certified_type_count == before.size(),
                "certified-bound cache has the wrong count");
        std::size_t previous_order = 0;
        for (std::size_t i = 0; i < before.size(); ++i) {
            require(context.certified_types[i] == before[i], "certified-bound cache changed order");
            const auto position = std::find(expected_order.begin(), expected_order.end(), before[i]);
            require(position != expected_order.end() &&
                        (i == 0 || static_cast<std::size_t>(position - expected_order.begin()) > previous_order),
                    "certified-bound order is not canonical");
            previous_order = static_cast<std::size_t>(position - expected_order.begin());
        }

        for (const Weight capacity : {Weight{1}, Weight{7}, Weight{19}, Weight{43}}) {
            const Instance instance{capacity, items};
            const Profit oracle = dense_dp_value(instance);
            const BoundValue expected = original_best_certified(context, capacity);
            for (const BoundPolicy policy : {BoundPolicy::U3, BoundPolicy::V, BoundPolicy::TauStar,
                                              BoundPolicy::BestItemStar, BoundPolicy::BestCertified}) {
                const BoundValue actual = compute_bound(context, capacity, policy);
                const faithful::detail::ContextualBound contextual =
                    faithful::detail::compute_contextual_bound(
                        context, capacity, policy, capacity / context.best.w);
                require(contextual.upper == actual.upper && contextual.type == actual.type,
                        "specialized contextual evaluator changed a bound");
                require(actual.upper >= oracle && actual.lower <= oracle,
                        "bound policy disagrees with dense-DP oracle");
                if (policy == BoundPolicy::BestCertified) {
                    require(actual.upper == expected.upper && actual.lower == expected.lower &&
                                actual.type == expected.type,
                            "BestCertified changed winner or stable tie break");
                } else {
                    const BoundType requested = policy == BoundPolicy::U3 ? BoundType::U3 :
                        policy == BoundPolicy::V ? BoundType::V :
                        policy == BoundPolicy::TauStar ? BoundType::TauStar : BoundType::BestItemStar;
                    const BoundType expected_type = is_bound_certified(context, requested) ?
                        requested : before.front();
                    const BoundValue forced_expected =
                        original_individual_bound(context, capacity, expected_type);
                    require(actual.upper == forced_expected.upper && actual.lower == forced_expected.lower &&
                                actual.type == forced_expected.type,
                            "forced policy changed its certified fallback");
                }
            }
        }
    }
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

void check_exnsds12_incremental_regression() {
    const auto path = std::filesystem::path(UKP_SOURCE_DIR) / "data" / "exnsds12.ukp";
    const Instance instance = read_instance_file(path.string());
    const SolverResult result = faithful::Solver(SolverOptions{}).solve(instance);
    require(verify_solution(instance, result.solution) && result.solution.profit == 3'793'952,
            "exnsds12 incremental regression changed the optimum");
    require(result.stats.items_considered_for_introduction == result.stats.after_preprocess_items,
            "exnsds12 did not consider the full residual item set by weight");
    require(result.stats.items_introduced <= 250 &&
                result.stats.items_introduced * 10 < result.stats.after_preprocess_items,
            "exnsds12 reactivated the eager full-item DP");
    require(result.stats.successor_item_scans < 3'000'000,
            "exnsds12 successor fan-out regressed");
    require(result.stats.dp_stop_reason == "periodicity" &&
                result.stats.periodicity_hits == 1 &&
                result.stats.periodicity_level >= 0 &&
                result.stats.periodicity_level < (instance.capacity + 1) / 2,
            "exnsds12 did not use the EDUK2 early periodicity certificate");

    SolverOptions without_periodicity;
    without_periodicity.use_periodicity = false;
    const SolverResult reference = faithful::Solver(without_periodicity).solve(instance);
    require(reference.stats.periodicity_hits == 0 && reference.stats.periodicity_level == -1,
            "disabled periodicity still changed DP termination");
    require(result.solution.profit == reference.solution.profit &&
                result.solution.weight == reference.solution.weight &&
                result.solution.multiplicity_by_id == reference.solution.multiplicity_by_id,
            "periodic fill changed the exnsds12 optimum or reconstruction");
}

void check_moderate_cursor_regression() {
    const auto path = std::filesystem::path(UKP_SOURCE_DIR) / "data" /
        "ukp_moderate_bb_2000_900k.ukp";
    const Instance instance = read_instance_file(path.string());
    const SolverResult result = faithful::Solver(SolverOptions{}).solve(instance);
    require(verify_solution(instance, result.solution) &&
                result.solution.profit == 928'539 &&
                result.solution.weight == 900'000,
            "moderate cursor regression changed the optimum");
    std::vector<long long> expected_multiplicity(instance.items.size(), 0);
    expected_multiplicity[10] = 1;
    expected_multiplicity[14] = 1;
    expected_multiplicity[18] = 11;
    expected_multiplicity[1996] = 11;
    require(result.solution.multiplicity_by_id == expected_multiplicity,
            "moderate cursor regression changed reconstruction");
    require(result.stats.states_scanned == 49'135 &&
                result.stats.states_expanded == 47'491 &&
                result.stats.periodicity_level == 180'720,
            "moderate cursor regression changed states or periodicity");
    require(result.stats.backfill_attempts == 12'295'333 &&
                result.stats.successor_attempts == 18'576'637 &&
                result.stats.cursor_advances == result.stats.successor_attempts &&
                result.stats.historical_states_avoided == 4'932'363 &&
                result.stats.successor_attempts +
                    result.stats.historical_states_avoided == 23'509'000,
            "moderate cursor regression changed cursor work");
    require(result.stats.successor_attempts < 23'509'000,
            "moderate cursor scheduling reverted to eager future successors");
}

}  // namespace

int main() {
    try {
        check_safe_arithmetic();
        check_input_parser_compatibility();
        check_sparse_incumbent_updates();
        check_greedy_completion_equivalence();
        check_article_examples();
        check_generated_families();
        check_pyasukp_corpus();
        check_faithful_extended_prefix_regression();
        check_exnsds12_incremental_regression();
        check_moderate_cursor_regression();
        check_precomputed_q_star();
        check_ratio_ordered_context_rebuild();
        check_multiple_dominance_witness_persistence();
        check_linear_ratio_selection_equivalence();
        check_certified_bound_cache_and_policies();
        check_faithful_switches();
        check_faithful_core_selection();
        check_skip_point_sequence();
        check_equal_profit_predecessor_order();
        check_incremental_item_introduction();
        check_faithful_unordered_valid_ids();
        std::cout << "faithful correctness suite passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
