#include "ukp/faithful_solver.hpp"
#include "ukp/dominance.hpp"
#include "eduk2_bounds.hpp"
#include "critical_sequence.hpp"
#include "preprocessing.hpp"
#include "incumbent.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <map>
#include <numeric>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ukp::faithful {
namespace {

constexpr bool kBasicStats = stats_enabled_v<StatsMode::Basic>;
constexpr bool kFullStats = stats_enabled_v<StatsMode::Full>;

struct EmptySliceStats {
    Weight begin = 0;
    Weight end = 0;
    long long states_entered = 0;
    long long states_expanded = 0;
    long long successor_attempts = 0;
    long long successor_item_scans = 0;
    long long backfill_attempts = 0;
    long long cursor_advances = 0;
    long long historical_states_avoided = 0;
    long long states_created = 0;
    long long states_kept = 0;
    long long states_fathomed_by_bound = 0;
    long long items_removed_threshold = 0;
    long long active_items_before = 0;
    long long active_items_after = 0;
    long long items_considered_for_introduction = 0;
    long long items_introduced = 0;
    long long items_rejected_by_envelope = 0;
    long long items_rejected_by_bound = 0;
};
using LocalSliceStats = std::conditional_t<kFullStats, SliceStats, EmptySliceStats>;


template <bool Enabled, typename Slice>
void publish_slice_stats(std::vector<SliceStats>& destination, Slice&& slice) {
    if constexpr (Enabled) {
        destination.push_back(std::forward<Slice>(slice));
    } else {
        (void)destination;
        (void)slice;
    }
}

template <bool Enabled>
class PhaseTimer;

template <>
class PhaseTimer<true> {
public:
    explicit PhaseTimer(long long& accumulator) noexcept
        : accumulator_(accumulator), start_(Clock::now()) {}

    PhaseTimer(const PhaseTimer&) = delete;
    PhaseTimer& operator=(const PhaseTimer&) = delete;

    ~PhaseTimer() { stop(); }

    void stop() noexcept {
        if (!running_) return;
        accumulator_ += std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - start_).count();
        running_ = false;
    }

private:
    using Clock = std::chrono::steady_clock;
    long long& accumulator_;
    Clock::time_point start_;
    bool running_ = true;
};

template <>
class PhaseTimer<false> {
public:
    explicit PhaseTimer(long long&) noexcept {}
    void stop() noexcept {}
};

#define UKP_BASIC_STATS(...) \
    do { if constexpr (kBasicStats) { __VA_ARGS__; } } while (false)
#define UKP_FULL_STATS(...) \
    do { if constexpr (kFullStats) { __VA_ARGS__; } } while (false)

// Accumulates all residual-set changes requested during one DP slice.  The
// request tables deduplicate IDs without sorting, so dp_items remains the
// single source of the stable better_ratio order used to rebuild the context.
struct ResidualDelta {
    explicit ResidualDelta(std::size_t item_count)
        : removal_requested_by_id(item_count, 0),
          active_removal_requested_by_id(item_count, 0) {}

    void begin_slice() {
        for (const int item_id : removed_ids) {
            removal_requested_by_id[static_cast<std::size_t>(item_id)] = 0;
        }
        for (const detail::ActiveItem& item : active_items_to_remove) {
            active_removal_requested_by_id[static_cast<std::size_t>(item.id)] = 0;
        }
        removed_ids.clear();
        active_items_to_remove.clear();
        incumbent_changed = false;
        had_item_decisions = false;
        duplicate_requests = 0;
    }

    bool request_removal(int item_id) {
        if (item_id < 0 ||
            static_cast<std::size_t>(item_id) >= removal_requested_by_id.size()) {
            throw std::logic_error("residual removal item id is out of range");
        }
        unsigned char& requested =
            removal_requested_by_id[static_cast<std::size_t>(item_id)];
        if (requested != 0) {
            ++duplicate_requests;
            return false;
        }
        requested = 1;
        removed_ids.push_back(item_id);
        return true;
    }

    void request_threshold_removal(const detail::ActiveItem& item) {
        request_removal(item.id);
        unsigned char& active_requested = active_removal_requested_by_id[
            static_cast<std::size_t>(item.id)];
        if (active_requested == 0) {
            active_requested = 1;
            active_items_to_remove.push_back(item);
        } else {
            ++duplicate_requests;
        }

    }

    [[nodiscard]] bool contains(int item_id) const {
        if (item_id < 0 ||
            static_cast<std::size_t>(item_id) >= removal_requested_by_id.size()) {
            throw std::logic_error("residual query item id is out of range");
        }
        return removal_requested_by_id[static_cast<std::size_t>(item_id)] != 0;
    }

    [[nodiscard]] bool has_removals() const noexcept {
        return !removed_ids.empty();
    }

    [[nodiscard]] bool requested() const noexcept {
        return had_item_decisions || has_removals() ||
               !active_items_to_remove.empty();
    }

    std::vector<int> removed_ids;
    std::vector<detail::ActiveItem> active_items_to_remove;
    std::vector<unsigned char> removal_requested_by_id;
    std::vector<unsigned char> active_removal_requested_by_id;
    bool incumbent_changed = false;
    bool had_item_decisions = false;
    long long duplicate_requests = 0;
};

struct Cell {
    Profit profit = 0;
    int last_item = -1;
    Weight prev_capacity = 0;
};

constexpr std::size_t bound_type_index(BoundType type) {
    switch (type) {
        case BoundType::U3: return 0;
        case BoundType::V: return 1;
        case BoundType::TauStar: return 2;
        case BoundType::BestItemStar: return 3;
        case BoundType::Both: return 4;
    }
    return 4;
}

constexpr std::array<BoundType, 5> kBoundTypes{
    BoundType::U3, BoundType::V, BoundType::TauStar,
    BoundType::BestItemStar, BoundType::Both};

Profit run_core_bb_rec(const std::vector<Item>& core, Weight c, size_t k,
                       Profit cur_profit, Weight cur_weight,
                       Profit incumbent, long long& nodes, long long limit) {
    if (nodes >= limit || k >= core.size()) return std::max(incumbent, cur_profit);
    ++nodes;
    const Item& it = core[k];
    long long max_x = (c - cur_weight) / it.w;
    for (long long x = max_x; x >= 0; --x) {
        Profit np = cur_profit + safe_mul(x, it.p);
        Weight nw = cur_weight + x * it.w;
        incumbent = std::max(incumbent, np);
        if (k + 1 < core.size()) {
            Profit ub = np + floor_mul_div(c - nw, core[k + 1].p, core[k + 1].w);
            if (ub > incumbent) {
                incumbent = run_core_bb_rec(core, c, k + 1, np, nw, incumbent, nodes, limit);
            }
        }
        if (x == 0) break;
    }
    return incumbent;
}

[[maybe_unused]] Profit run_core_bb(std::vector<Item> items, Weight c, int requested_core,
                                    long long node_limit, long long& nodes) {
    std::sort(items.begin(), items.end(), better_ratio);
    const int default_core_size = std::min<int>(static_cast<int>(items.size()),
                                                std::max(100, static_cast<int>(items.size() / 100)));
    int core_size = requested_core > 0 ? requested_core : default_core_size;
    core_size = std::max(1, std::min<int>(core_size, static_cast<int>(items.size())));
    items.resize(static_cast<size_t>(core_size));
    nodes = 0;
    return run_core_bb_rec(items, c, 0, 0, 0, 0, nodes, node_limit);
}

struct CoreSearchResult {
    Profit profit = 0;
    std::vector<long long> multiplicity;
    long long nodes = 0;
    bool closed = false;
    long long multiple_removed = 0;
    long long modular_removed = 0;
};

struct EffectiveOptions {
    bool simple_dominance = false;
    bool core_remainder_ordering = false;
    bool modular_dominance = false;
    bool core_multiple_dominance = false;
};

EffectiveOptions effective_options(const SolverOptions& options) {
    if (options.paper_faithful_mode) return {};
    return {options.use_simple_dominance, options.use_core_remainder_ordering,
            options.use_modular_dominance, options.use_core_multiple_dominance};
}

void core_search(const std::vector<Item>& core, const BoundContext& bounds, BoundPolicy policy,
                 Weight capacity, Profit global_upper, long long node_limit,
                 std::size_t k, Weight used_weight, Profit used_profit,
                 std::vector<long long>& current, CoreSearchResult& result) {
    if (result.closed || result.nodes >= node_limit) return;
    ++result.nodes;
    if (used_profit > result.profit) {
        result.profit = used_profit;
        result.multiplicity = current;
        if (result.profit >= global_upper) {
            result.closed = true;
            return;
        }
    }
    if (k == core.size()) return;
    const Item& item = core[k];
    const Weight remaining = capacity - used_weight;
    for (long long count = remaining / item.w; count >= 0; --count) {
        const Weight next_weight = used_weight + safe_mul(count, item.w);
        const Profit next_profit = safe_add(used_profit, safe_mul(count, item.p));
        const Profit upper = safe_add(next_profit,
            compute_bound(bounds, capacity - next_weight, policy).upper);
        if (upper > result.profit) {
            current[static_cast<std::size_t>(item.id)] = count;
            core_search(core, bounds, policy, capacity, global_upper, node_limit, k + 1,
                        next_weight, next_profit, current, result);
            current[static_cast<std::size_t>(item.id)] = 0;
        }
        if (count == 0 || result.closed || result.nodes >= node_limit) break;
    }
}

[[maybe_unused]] CoreSearchResult run_pyasukp_core_bb(std::vector<Item> items, const BoundContext& bounds,
                                                       Weight capacity, Profit global_upper,
                                                       int requested_core, long long node_limit,
                                                       std::size_t original_count) {
    std::sort(items.begin(), items.end(), better_ratio);
    const int default_size = std::min<int>(items.size(), std::max(500, static_cast<int>(items.size() / 100)));
    items.resize(static_cast<std::size_t>(std::max(1, std::min(requested_core > 0 ? requested_core : default_size,
                                                                static_cast<int>(items.size())))));
    CoreSearchResult result;
    result.multiplicity.assign(original_count, 0);
    std::vector<long long> current(original_count, 0);
    core_search(items, bounds, BoundPolicy::BestCertified, capacity, global_upper, std::max<long long>(0, node_limit),
                0, 0, 0, current, result);
    return result;
}

// Structural C++ counterpart of bandbukp2.ml: greedy_fill establishes the
// incumbent, complete explores a completion, and traverse controls the node
// budget.  The state table is the Dynefflist equivalent: for equal used weight
// only the greatest profit can lead to a useful residual subproblem.
struct CoreTraversal {
    const std::vector<Item>& items;
    const BoundContext& bounds;
    BoundPolicy policy;
    Weight capacity;
    Profit global_upper;
    long long limit;
    CoreSearchResult result;
    std::unordered_map<Weight, Profit> best_profit_at_weight;
    std::vector<Weight> min_remaining_weight;
};

std::vector<Weight> lightest_worse(const std::vector<Item>& items) {
    std::vector<Weight> minweights(items.size(), 0);
    if (items.size() < 2) return minweights;
    minweights[items.size() - 2] = items[items.size() - 1].w;
    for (std::size_t i = items.size() - 2; i > 0; --i) {
        minweights[i - 1] = std::min(items[i].w, minweights[i]);
    }
    return minweights;
}

bool remember_core_state(CoreTraversal& search, Weight used_weight, Profit used_profit) {
    const auto known = search.best_profit_at_weight.find(used_weight);
    if (known != search.best_profit_at_weight.end() && known->second >= used_profit) return false;
    search.best_profit_at_weight[used_weight] = used_profit;
    return true;
}

void record_core_solution(CoreTraversal& search, Profit profit,
                          const std::vector<long long>& multiplicity) {
    if (profit > search.result.profit) {
        search.result.profit = profit;
        search.result.multiplicity = multiplicity;
    }
    if (search.result.profit >= search.global_upper) search.result.closed = true;
}

void complete(CoreTraversal& search, std::size_t item_index, Weight used_weight,
              Profit used_profit, std::vector<long long>& multiplicity) {
    if (search.result.closed || search.result.nodes >= search.limit) return;
    ++search.result.nodes;
    if (!remember_core_state(search, used_weight, used_profit)) return;
    record_core_solution(search, used_profit, multiplicity);
    if (item_index + 1 >= search.items.size()) return;

    const Weight remaining = search.capacity - used_weight;
    if (item_index < search.min_remaining_weight.size() &&
        remaining < search.min_remaining_weight[item_index]) {
        return;
    }

    const Item& item = search.items[item_index];
    for (long long count = remaining / item.w; count >= 0; --count) {
        const Weight next_weight = used_weight + safe_mul(count, item.w);
        const Profit next_profit = safe_add(used_profit, safe_mul(count, item.p));
        const Profit residual_upper = compute_bound(search.bounds, search.capacity - next_weight, search.policy).upper;
        if (safe_add(next_profit, residual_upper) > search.result.profit) {
            multiplicity[static_cast<std::size_t>(item.id)] = count;
            complete(search, item_index + 1, next_weight, next_profit, multiplicity);
            multiplicity[static_cast<std::size_t>(item.id)] = 0;
        }
        if (count == 0 || search.result.closed || search.result.nodes >= search.limit) break;
    }
}

void greedy_fill(CoreTraversal& search, std::vector<long long>& multiplicity) {
    Weight used_weight = 0;
    Profit profit = 0;
    for (const Item& item : search.items) {
        const long long count = (search.capacity - used_weight) / item.w;
        if (count == 0) continue;
        for (long long copy = 0; copy < count; ++copy) {
            multiplicity[static_cast<std::size_t>(item.id)] += 1;
            used_weight += item.w;
            profit = safe_add(profit, item.p);
            remember_core_state(search, used_weight, profit);
        }
    }
    record_core_solution(search, profit, multiplicity);
    std::fill(multiplicity.begin(), multiplicity.end(), 0);
}

// `backtrack` is the MTU/PYAsUKP branch controller.  Multiplicity choices are
// undone by complete after every child, so the same vector can be reused.
void backtrack(CoreTraversal& search, std::vector<long long>& multiplicity) {
    complete(search, 0, 0, 0, multiplicity);
}

CoreSearchResult traverse_core(const std::vector<Item>& dp_items, const BoundContext& bounds, BoundPolicy policy,
                               Weight capacity, Profit global_upper, int requested_core,
                               long long limit, std::size_t original_count,
                               const EffectiveOptions& effective, bool paper_faithful_mode,
                               BoundContextTelemetry* context_telemetry,
                               std::vector<int>* selected_core_ids = nullptr) {
    std::vector<Item> core_items;
    if (paper_faithful_mode) {
        // dp_items is already globally reduced and sorted by better_ratio.
        // The faithful core is exactly its prefix: no local ordering or
        // filtering is permitted on this path.
        const std::size_t n = dp_items.size();
        const std::size_t core_size = std::min(n, std::max<std::size_t>(100, n / 100));
        core_items.assign(dp_items.begin(), dp_items.begin() + core_size);
    } else {
        core_items = dp_items;
        const int default_size = std::min<int>(core_items.size(), std::max(100, static_cast<int>(core_items.size() / 100)));
        const int core_size = std::max(1, std::min(requested_core > 0 ? requested_core : default_size,
                                                    static_cast<int>(core_items.size())));
        if (effective.core_remainder_ordering) {
            std::sort(core_items.begin(), core_items.end(), [capacity](const Item& left, const Item& right) {
                const Weight left_remainder = capacity % left.w;
                const Weight right_remainder = capacity % right.w;
                if (left_remainder != right_remainder) return left_remainder < right_remainder;
                return better_ratio(left, right);
            });
        } else {
            std::sort(core_items.begin(), core_items.end(), better_ratio);
        }
        core_items.resize(static_cast<std::size_t>(core_size));
    }
    if (selected_core_ids != nullptr) {
        selected_core_ids->clear();
        selected_core_ids->reserve(core_items.size());
        for (const Item& item : core_items) selected_core_ids->push_back(item.id);
    }

    std::vector<Item> filtered = core_items;
    long long core_multiple_removed = 0;
    long long core_modular_removed = 0;
    if (!paper_faithful_mode) {
        filtered.clear();
        // Core-local reductions are experimental and apply only to this copy.
        for (const Item& candidate : core_items) {
            bool dominated = false;
            bool modular = false;
            for (const Item& kept : filtered) {
                if (effective.core_multiple_dominance) {
                    const long long copies = candidate.w / kept.w;
                    if (copies > 0 && safe_mul(copies, kept.p) >= candidate.p) { dominated = true; break; }
                }
                if (effective.modular_dominance && kept.w <= candidate.w && bounds.best.w != candidate.w) {
                    const Weight candidate_remainder = candidate.w % bounds.best.w;
                    const Weight kept_remainder = kept.w % bounds.best.w;
                    const Profit kept_z = safe_add(safe_mul(kept.w, bounds.best.p),
                                                    -safe_mul(bounds.best.w, kept.p));
                    const Profit candidate_z = safe_add(safe_mul(candidate.w, bounds.best.p),
                                                         -safe_mul(bounds.best.w, candidate.p));
                    if (candidate_remainder == 0 ||
                        (candidate_remainder == kept_remainder && kept_z <= candidate_z)) {
                        dominated = true;
                        modular = true;
                        break;
                    }
                }
            }
            if (!dominated) {
                filtered.push_back(candidate);
            } else if (modular) {
                ++core_modular_removed;
            } else {
                ++core_multiple_removed;
            }
        }
    }
    // Bounds in the core must describe its locally filtered items, while the
    // global upper remains the global certificate used for closure.
    BoundContext core_bounds = make_bound_context(filtered, context_telemetry);
    CoreTraversal search{filtered, core_bounds, policy, capacity, global_upper, std::max<long long>(0, limit), {}, {},
                         lightest_worse(filtered)};
    search.result.multiple_removed = core_multiple_removed;
    search.result.modular_removed = core_modular_removed;
    search.result.multiplicity.assign(original_count, 0);
    std::vector<long long> multiplicity(original_count, 0);
    greedy_fill(search, multiplicity);
    backtrack(search, multiplicity);
    return search.result;
}

Solution solution_from_best_item(const Instance& inst, const Item& best, long long count) {
    Solution sol;
    sol.profit = safe_mul(count, best.p);
    sol.weight = count * best.w;
    sol.optimal = true;
    sol.solver_name = "faithful";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);
    if (best.id >= 0 && static_cast<std::size_t>(best.id) < sol.multiplicity_by_id.size()) {
        sol.multiplicity_by_id[static_cast<std::size_t>(best.id)] = count;
    }
    return sol;
}

}  // namespace

Solver::Solver(SolverOptions options) : options_(options) {}

SolverResult Solver::solve(const Instance& inst) {
    if (inst.capacity < 0) throw std::invalid_argument("negative capacity");
    SolverResult result;
    const EffectiveOptions effective = effective_options(options_);
    UKP_BASIC_STATS(
        result.stats.original_items = static_cast<long long>(inst.items.size());
    );
    UKP_FULL_STATS(
        result.stats.backfill_attempts_by_item.assign(inst.items.size(), -1);
    );
    if (inst.items.empty() || inst.capacity == 0) {
        result.solution.multiplicity_by_id.assign(inst.items.size(), 0);
        result.solution.optimal = true;
        result.solution.solver_name = "faithful";
        UKP_BASIC_STATS(
            result.stats.stop_reason = "empty_instance";
            result.stats.dp_stop_reason = "empty_instance";
        );
        return result;
    }

    detail::PreprocessResult preprocessing;
    {
        PhaseTimer<kFullStats> phase(result.stats.phase_preprocessing_ns);
        preprocessing = detail::preprocess_items(inst, effective.simple_dominance);
    }
    std::vector<Item> items = std::move(preprocessing.items);
    UKP_BASIC_STATS(
        result.stats.items_removed_simple = preprocessing.simple_removed;
        result.stats.items_removed_multiple = preprocessing.multiple_removed;
        result.stats.after_preprocess_items = static_cast<long long>(items.size());
    );

    BoundContextTelemetry context_telemetry;
    BoundContextTelemetry* context_telemetry_ptr = nullptr;
    detail::BoundDecisionTelemetry bound_decision_telemetry;
    detail::BoundDecisionTelemetry* bound_decision_telemetry_ptr = nullptr;
    if constexpr (kFullStats) {
        context_telemetry_ptr = &context_telemetry;
        bound_decision_telemetry_ptr = &bound_decision_telemetry;
    }
    auto publish_context_telemetry = [&]() {
        if constexpr (kFullStats) {
            result.stats.bound_context_rebuilds = context_telemetry.rebuilds;
            result.stats.bound_context_items_processed = context_telemetry.items_processed;
            result.stats.bound_context_tau_q_recomputations =
                context_telemetry.tau_q_recomputations;
            result.stats.bound_context_tau_q_items_scanned =
                context_telemetry.tau_q_items_scanned;
            result.stats.bound_context_best_q_recomputations =
                context_telemetry.best_q_recomputations;
            result.stats.bound_context_best_q_items_scanned =
                context_telemetry.best_q_items_scanned;
            result.stats.bound_context_alpha_recomputations =
                context_telemetry.alpha_recomputations;
            result.stats.bound_context_alpha_items_scanned =
                context_telemetry.alpha_items_scanned;
            result.stats.bound_context_dominance_full_searches =
                context_telemetry.dominance_full_searches;
            result.stats.bound_context_dominance_searches_avoided_by_witness =
                context_telemetry.dominance_searches_avoided_by_witness;
            result.stats.bound_context_dominance_witness_invalidations =
                context_telemetry.dominance_witness_invalidations;
            result.stats.bound_context_dominance_pair_checks =
                context_telemetry.dominance_pair_checks;
            result.stats.lower_filter_hits =
                bound_decision_telemetry.lower_filter_hits;
            result.stats.bounds_short_circuited =
                bound_decision_telemetry.bounds_short_circuited;
            for (std::size_t index = 0; index < 4; ++index) {
                if (bound_decision_telemetry.bounds_evaluated[index] != 0) {
                    result.stats.bounds_evaluated[
                        ::ukp::bound_type_name(kBoundTypes[index])] =
                            bound_decision_telemetry.bounds_evaluated[index];
                }
            }
        }
    };
    PhaseTimer<kFullStats> global_bounds_phase(result.stats.phase_global_bounds_ns);
    long long discarded_bound_calls = 0;
    long long& bound_calls_counter = kBasicStats
        ? result.stats.bound_calls : discarded_bound_calls;
    BoundContext ctx;
    BoundValue global_bound{};
    global_bound.upper = std::numeric_limits<Profit>::max();
    long long best_count = 0;
    Profit incumbent = 0;
    if (options_.use_bounds) {
        // initialize_bounds is the only initial BoundContext construction on
        // the bounded path.  The previous code built the same context twice.
        detail::BoundPhase bound_phase = detail::initialize_bounds(
            items, inst.capacity, options_.bound_policy, context_telemetry_ptr);
        ctx = std::move(bound_phase.context);
        global_bound = bound_phase.global;
        best_count = bound_phase.best_count;
        incumbent = bound_phase.incumbent;
        UKP_BASIC_STATS(
            ++result.stats.bound_calls;
            result.stats.global_bound_used = ::ukp::bound_type_name(global_bound.type);
            result.stats.bound_winner = result.stats.global_bound_used;
        );
    } else {
        ctx = make_bound_context(items, context_telemetry_ptr);
        best_count = inst.capacity / ctx.best.w;
        incumbent = safe_mul(best_count, ctx.best.p);
    }

    detail::Incumbent incumbent_solution(inst.items.size());
    incumbent_solution.consider(incumbent, safe_mul(best_count, ctx.best.w),
                                solution_from_best_item(inst, ctx.best, best_count).multiplicity_by_id,
                                detail::no_point, ctx.best.id, best_count);

    // EDUK2 bound-stop shortcut.  This is exact only when the certified incumbent
    // is the one reconstructed here, so it is applied before the core B&B changes
    // the incumbent without storing its multiplicities.
    if (options_.use_bounds && incumbent >= global_bound.upper) {
        result.solution = solution_from_best_item(inst, ctx.best, best_count);
        UKP_BASIC_STATS(
            result.stats.active_items_final = static_cast<long long>(items.size());
            result.stats.stop_reason = "initial_bound";
        );
        publish_context_telemetry();
        return result;
    }

    if (options_.use_bounds) {
        const long long items_before_bound_reduction = static_cast<long long>(items.size());
#ifndef NDEBUG
        assert(std::is_sorted(items.begin(), items.end(), better_ratio));
#endif
        items = detail::reduce_variables_by_bound(
            items, ctx, inst.capacity, incumbent, bound_calls_counter,
            options_.bound_policy, bound_decision_telemetry_ptr);
        // reduce_variables_by_bound is a stable filter, so survivors retain
        // the exact better_ratio order of the input.
#ifndef NDEBUG
        assert(std::is_sorted(items.begin(), items.end(), better_ratio));
#endif
        UKP_BASIC_STATS(
            result.stats.items_removed_bound =
                items_before_bound_reduction - static_cast<long long>(items.size());
        );
        rebuild_bound_context_ratio_ordered(ctx, items, context_telemetry_ptr);
        UKP_BASIC_STATS(
            result.stats.after_preprocess_items = static_cast<long long>(items.size());
        );
    }
    global_bounds_phase.stop();

    // This is the global post-reduction list used by the DP.  The core B&B
    // only receives a const view and performs every experimental reduction on
    // its own local copy.
    const std::vector<Item> dp_items = items;
    // Gilmore-Gomory periodicity, as used by EDUK2, is defined with respect
    // to the ratio-best item.  Keep this witness stable while the residual
    // bound context shrinks during the DP.
    const Item periodic_best = dp_items.front();
    if constexpr (kFullStats) {
        result.stats.dp_item_ids.reserve(dp_items.size());
        for (const Item& item : dp_items) result.stats.dp_item_ids.push_back(item.id);
    }

    // The residual bound set is monotone: a candidate leaves it only after an
    // envelope/bound rejection or threshold removal.  Keep membership by id
    // and rebuild in dp_items' stable ratio order, avoiding the old active +
    // weight-ordered concatenation and its full sort on every change.
    std::vector<unsigned char> residual_item_alive(inst.items.size(), 0);
    auto residual_slot = [&](const auto& item) -> unsigned char& {
        if (item.id < 0 || static_cast<std::size_t>(item.id) >= residual_item_alive.size()) {
            throw std::logic_error("item id is outside residual bound membership");
        }
        return residual_item_alive[static_cast<std::size_t>(item.id)];
    };
    for (const Item& item : dp_items) residual_slot(item) = 1;
    std::vector<Item> residual_items;
    residual_items.reserve(dp_items.size());
    if (options_.use_core_bb) {
        PhaseTimer<kFullStats> core_bb_phase(result.stats.phase_core_bb_ns);
        constexpr long long kFaithfulCoreNodeLimit = 10'000;
        const long long core_node_limit = options_.paper_faithful_mode
            ? kFaithfulCoreNodeLimit : options_.bb_node_limit;
        UKP_BASIC_STATS(result.stats.core_node_limit = core_node_limit;);
        std::vector<int>* selected_core_ids = nullptr;
        if constexpr (kFullStats) selected_core_ids = &result.stats.core_item_ids;
        CoreSearchResult core = traverse_core(dp_items, ctx, options_.bound_policy, inst.capacity,
                                               global_bound.upper, options_.core_size,
                                               core_node_limit, inst.items.size(), effective,
                                               options_.paper_faithful_mode, context_telemetry_ptr,
                                               selected_core_ids);
        incumbent = std::max(incumbent, core.profit);
        UKP_BASIC_STATS(
            if (core.profit > incumbent_solution.profit) {
                ++result.stats.incumbent_improvements_bb;
            }
        );
        Weight core_weight = 0;
        for (const Item& item : inst.items) {
            core_weight += safe_mul(core.multiplicity[static_cast<std::size_t>(item.id)], item.w);
        }
        incumbent_solution.consider(core.profit, core_weight, core.multiplicity);
        UKP_BASIC_STATS(
            result.stats.bb_nodes += core.nodes;
            result.stats.items_removed_core_multiple += core.multiple_removed;
            result.stats.items_removed_modular += core.modular_removed;
        );
        if (core.closed) {
            result.solution = incumbent_solution.solution("faithful");
            UKP_BASIC_STATS(
                result.stats.active_items_final = static_cast<long long>(dp_items.size());
                result.stats.stop_reason = "core_bound_closed";
            );
            publish_context_telemetry();
            return result;
        }
    }

    PhaseTimer<kFullStats> dp_phase(result.stats.phase_dp_ns);

    // Select.next_lightest traverses the residual items by nondecreasing
    // weight.  Equal-weight candidates retain the ratio/id order already
    // prescribed by better_ratio, so an inferior duplicate is never made
    // active before its dominating peer.
    std::vector<detail::ActiveItem> items_by_weight;
    items_by_weight.reserve(dp_items.size());
    for (std::size_t rank = 0; rank < dp_items.size(); ++rank) {
        const Item& item = dp_items[rank];
        if (item.w <= inst.capacity) {
            items_by_weight.push_back(detail::ActiveItem{
                item.id, static_cast<int>(rank), item.w, item.p});
        }
    }
    std::stable_sort(items_by_weight.begin(), items_by_weight.end(),
        [](const detail::ActiveItem& left, const detail::ActiveItem& right) {
            if (left.w != right.w) return left.w < right.w;
            return left.tie_rank < right.tie_rank;
        });
    std::size_t next_item = 0;

    // `active_items` has the same role as PYAsUKP's decreasingS: it contains
    // only introduced, threshold-undominated items and stays ratio ordered.
    std::vector<detail::ActiveItem> active_items;
    active_items.reserve(dp_items.size());

    // The sequence is the DP representation: it stores only strict increases
    // of f(N, y), in topological weight order.
    detail::CriticalSequence sequence;
    sequence.configure_item_order(dp_items);
    constexpr Weight kNoContribution = std::numeric_limits<Weight>::min();
    std::vector<Weight> last_contribution(inst.items.size(), kNoContribution);
    auto contribution_slot = [&](int item_id) -> Weight& {
        if (item_id < 0 || static_cast<std::size_t>(item_id) >= last_contribution.size()) {
            throw std::logic_error("item id is outside contribution table");
        }
        return last_contribution[static_cast<std::size_t>(item_id)];
    };
    const Weight wmin = items_by_weight.empty() ? Weight{1} : items_by_weight.front().w;
    // PYAsUKP's executable defaults layer_height to 100 and then takes the
    // maximum with the lightest weight during reduction.
    Weight h = options_.slice_height > 0 ? options_.slice_height : std::max<Weight>(100, wmin);
    if (h <= 0) h = 1;
    const Weight half_capacity = (inst.capacity + 1) / 2;
    const Weight introduction_limit = items_by_weight.empty() ? Weight{0} : items_by_weight.back().w;
    // EDUK first finishes its reduction through the heaviest candidate item;
    // EDUK2 then reaches c/2 and extends once by the heaviest item that
    // survived threshold dominance.
    const Weight initial_process_limit = std::max(half_capacity, introduction_limit);
    const Weight candidate_limit = inst.capacity;
    Weight process_limit = initial_process_limit;
    constexpr Weight kMaximumSliceReserve = 1U << 16;
    const __int128 desired_process_limit =
        static_cast<__int128>(initial_process_limit) + introduction_limit;
    const Weight expected_process_limit = desired_process_limit >= inst.capacity
        ? inst.capacity : static_cast<Weight>(desired_process_limit);
    const Weight estimated_slice_count = std::min(
        kMaximumSliceReserve, expected_process_limit / h + 2);
    UKP_FULL_STATS(
        result.stats.slices.reserve(static_cast<std::size_t>(estimated_slice_count));
    );
    bool half_capacity_extension_done = false;
    bool closed_by_bound = false;
    bool periodicity_detected = false;
    detail::PointId periodicity_base = detail::no_point;
    long long periodicity_best_copies = 0;
    std::array<long long, kBoundTypes.size()> contextual_bound_counts{};
    std::array<long long, kBoundTypes.size()> contextual_bound_evaluations{};
    std::array<long long, kBoundTypes.size()> contextual_bound_state_wins{};
    std::array<long long, kBoundTypes.size()> contextual_bound_item_wins{};
    std::array<long long, kBoundTypes.size()> contextual_bound_fathoms{};
    auto record_contextual_bound = [&](const detail::BoundDecision& decision,
                                       auto& slice) {
        if constexpr (kFullStats) {
            for (std::size_t index = 0; index < 4; ++index) {
                if ((decision.evaluated_mask & (std::uint8_t{1} << index)) != 0) {
                    ++contextual_bound_evaluations[index];
                }
            }
            if (decision.evaluated_mask == 0 || decision.witness == BoundType::Both) {
                return;
            }
            ++contextual_bound_counts[bound_type_index(decision.witness)];
            const char* name = ::ukp::bound_type_name(decision.witness);
            if (slice.contextual_bound_used != name) slice.contextual_bound_used = name;
        } else {
            (void)decision;
            (void)slice;
        }
    };
    auto record_bound_decision = [&](const detail::BoundDecision& decision) {
        if constexpr (kFullStats) {
            detail::accumulate_bound_decision_telemetry(
                bound_decision_telemetry_ptr, decision);
        } else {
            (void)decision;
        }
    };
    auto publish_dp_telemetry = [&]() {
        if constexpr (kFullStats) {
            for (std::size_t index = 0; index < kBoundTypes.size(); ++index) {
                if (contextual_bound_counts[index] != 0) {
                    result.stats.contextual_bound_calls[
                        ::ukp::bound_type_name(kBoundTypes[index])] =
                            contextual_bound_counts[index];
                    result.stats.contextual_bound_wins[
                        ::ukp::bound_type_name(kBoundTypes[index])] =
                            contextual_bound_counts[index];
                }
                if (contextual_bound_evaluations[index] != 0) {
                    result.stats.contextual_bound_evaluations[
                        ::ukp::bound_type_name(kBoundTypes[index])] =
                            contextual_bound_evaluations[index];
                }
                if (contextual_bound_state_wins[index] != 0) {
                    result.stats.contextual_bound_state_wins[
                        ::ukp::bound_type_name(kBoundTypes[index])] =
                            contextual_bound_state_wins[index];
                }
                if (contextual_bound_item_wins[index] != 0) {
                    result.stats.contextual_bound_item_wins[
                        ::ukp::bound_type_name(kBoundTypes[index])] =
                            contextual_bound_item_wins[index];
                }
                if (contextual_bound_fathoms[index] != 0) {
                    result.stats.contextual_bound_fathoms[
                        ::ukp::bound_type_name(kBoundTypes[index])] =
                            contextual_bound_fathoms[index];
                }
            }
            result.stats.candidates_stored = sequence.candidates_stored();
            result.stats.computed_window_collisions =
                sequence.computed_window_collisions();
            result.stats.computed_window_replacements =
                sequence.computed_window_replacements();
            result.stats.computed_window_rejections =
                sequence.computed_window_rejections();
            result.stats.computed_window_index_collisions =
                sequence.computed_window_index_collisions();
            publish_context_telemetry();
        }
    };
    // Most greedy completions do not improve the incumbent.  Keep one
    // reconstruction buffer for the rare candidates that do, and remember the
    // positions written so clearing it does not become another O(n) pass.
    std::vector<long long> reconstruction_multiplicity;
    std::vector<int> reconstruction_touched_ids;
    reconstruction_touched_ids.reserve(dp_items.size());

    // Greedy completion retains the active-item order.  This suffix minimum
    // only lets it stop once no remaining item can fit; it never changes
    // which item would be considered next.
    std::vector<Weight> active_suffix_min_weight;
    auto rebuild_active_suffix_min_weight = [&]() {
        UKP_FULL_STATS(++result.stats.suffix_rebuilds;);
        active_suffix_min_weight.resize(active_items.size());
        if (active_items.empty()) return;
        Weight minimum_weight = active_items.back().w;
        for (std::size_t index = active_items.size(); index-- > 0;) {
            minimum_weight = std::min(minimum_weight, active_items[index].w);
            active_suffix_min_weight[index] = minimum_weight;
        }
    };
    rebuild_active_suffix_min_weight();

    auto ensure_active_suffix_min_weight = [&]() {
        if (active_suffix_min_weight.size() != active_items.size()) {
            rebuild_active_suffix_min_weight();
        }
    };

    ResidualDelta residual_delta(inst.items.size());
    std::vector<detail::ActiveItem> residual_survivors;
    residual_survivors.reserve(dp_items.size());

    auto consider_greedy_completion = [&](detail::PointId state_index) {
        ensure_active_suffix_min_weight();
        const detail::State& state = sequence.state(state_index);
        Weight used_weight = state.weight;
        Weight remaining = inst.capacity - used_weight;
        Profit candidate_profit = state.profit;
        for (std::size_t index = 0; index < active_items.size(); ++index) {
            if (remaining < active_suffix_min_weight[index]) break;
            const detail::ActiveItem& item = active_items[index];
            if (item.w > remaining) continue;
            const long long copies = remaining / item.w;
            const Weight added_weight = safe_mul(copies, item.w);
            used_weight += added_weight;
            remaining -= added_weight;
            candidate_profit = safe_add(candidate_profit, safe_mul(copies, item.p));
        }

        // Keep this predicate in sync with Incumbent::consider.  In particular,
        // a tied profit only wins when it uses more weight.
        if (candidate_profit < incumbent_solution.profit ||
            (candidate_profit == incumbent_solution.profit &&
             used_weight <= incumbent_solution.weight)) {
            return;
        }

        if (reconstruction_multiplicity.empty()) {
            reconstruction_multiplicity.assign(inst.items.size(), 0);
        }
        for (detail::PointId cursor = state_index; cursor != detail::no_point;
             cursor = sequence.state(cursor).predecessor) {
            const int item_id = sequence.state(cursor).item_id;
            if (item_id < 0) break;
            long long& count = reconstruction_multiplicity[static_cast<std::size_t>(item_id)];
            if (count == 0) reconstruction_touched_ids.push_back(item_id);
            ++count;
        }
        Weight reconstruction_weight = state.weight;
        Weight reconstruction_remaining = inst.capacity - reconstruction_weight;
        for (std::size_t index = 0; index < active_items.size(); ++index) {
            if (reconstruction_remaining < active_suffix_min_weight[index]) break;
            const detail::ActiveItem& item = active_items[index];
            if (item.w > reconstruction_remaining) continue;
            const long long copies = reconstruction_remaining / item.w;
            long long& count = reconstruction_multiplicity[static_cast<std::size_t>(item.id)];
            if (count == 0) reconstruction_touched_ids.push_back(item.id);
            count += copies;
            const Weight added_weight = safe_mul(copies, item.w);
            reconstruction_weight += added_weight;
            reconstruction_remaining -= added_weight;
        }

        if (incumbent_solution.consider_sparse(candidate_profit, used_weight,
                                               reconstruction_multiplicity,
                                               reconstruction_touched_ids)) {
            incumbent = incumbent_solution.profit;
            residual_delta.incumbent_changed = true;
            UKP_BASIC_STATS(++result.stats.incumbent_improvements_dp;);
        }
        for (const int item_id : reconstruction_touched_ids) {
            reconstruction_multiplicity[static_cast<std::size_t>(item_id)] = 0;
        }
        reconstruction_touched_ids.clear();
    };

    auto commit_residual_delta = [&](bool solver_will_continue) {
        if (!residual_delta.requested()) return;

        UKP_FULL_STATS(
            ++result.stats.residual_transactions;
            result.stats.duplicate_removal_requests +=
                residual_delta.duplicate_requests;
        );

        long long removed_now = 0;
        for (const int item_id : residual_delta.removed_ids) {
            unsigned char& alive =
                residual_item_alive[static_cast<std::size_t>(item_id)];
            if (alive == 0) {
                UKP_FULL_STATS(++result.stats.duplicate_removal_requests;);
                continue;
            }
            alive = 0;
            ++removed_now;
        }
        UKP_FULL_STATS(result.stats.residual_items_removed += removed_now;);

        // Threshold dominance is decided only after the slice has been fully
        // materialized. Removing the item from active_items here preserves all
        // candidates from this slice and permanently stops its cursor before
        // the next one.
        if (!residual_delta.active_items_to_remove.empty()) {
            for (const detail::ActiveItem& item :
                 residual_delta.active_items_to_remove) {
                sequence.stop_item_after_slice(item);
            }
            residual_survivors.clear();
            for (const detail::ActiveItem& item : active_items) {
                if (!residual_delta.contains(item.id)) {
                    residual_survivors.push_back(item);
                }
            }
            if (residual_survivors.size() != active_items.size()) {
                active_items.swap(residual_survivors);
                rebuild_active_suffix_min_weight();
            }
        }

        // A terminal slice never queries the context again.  With bounds
        // disabled, the shrinking residual context is likewise unused.
        if (!solver_will_continue || !options_.use_bounds) return;

        UKP_FULL_STATS(++result.stats.context_rebuilds_requested;);
        if (removed_now == 0) {
            UKP_FULL_STATS(++result.stats.context_rebuilds_skipped_no_change;);
            return;
        }

        residual_items.clear();
        for (const Item& item : dp_items) {
            if (residual_slot(item) != 0) residual_items.push_back(item);
        }
        if (residual_items.empty()) {
            throw std::logic_error("residual transaction removed every item");
        }
#ifndef NDEBUG
        assert(std::is_sorted(
            residual_items.begin(), residual_items.end(), better_ratio));
#endif
        rebuild_bound_context_ratio_ordered(
            ctx, residual_items, context_telemetry_ptr);
    };

    // Listing 1 mapping: build/process a slice; fathom its states with
    // f(y)+U(c-y)<=z; greedily complete survivors; update contributions;
    // apply threshold dominance at the completed boundary; then test stopping.
    // After crossing c/2, extend once through the largest active-item range,
    // as in EDUK2's `standard` recurrence.
    for (Weight ya = 0; ya < process_limit;) {
        const Weight yb = std::min(process_limit, ya + h);
        residual_delta.begin_slice();
        LocalSliceStats slice;
        if constexpr (kFullStats) {
            slice.begin = ya;
            slice.end = yb;
            slice.active_items_before = static_cast<long long>(active_items.size());
        }
        const std::size_t previous_next_item = next_item;
        const detail::SliceBuildResult build = sequence.process_slice_incremental(
            ya, yb, candidate_limit, active_items, items_by_weight, next_item,
            [&](const detail::ActiveItem& item, Profit) {
                if (options_.use_bounds && item.id != periodic_best.id) {
                    const detail::BoundDecision decision =
                        detail::evaluate_candidate(
                            ctx, item.w, item.p, inst.capacity, incumbent,
                            options_.bound_policy);
                    record_bound_decision(decision);
                    if (decision.lower_filter_hit) {
                        UKP_FULL_STATS(
                            ++result.stats.contextual_bound_calls_avoided_by_lower;
                            ++result.stats.contextual_bound_item_calls_avoided_by_lower;
                        );
                    }
                    if (decision.evaluated_mask != 0) {
                        UKP_BASIC_STATS(++result.stats.bound_calls;);
                        UKP_FULL_STATS(++result.stats.contextual_bound_item_queries;);
                        record_contextual_bound(decision, slice);
                        UKP_FULL_STATS(
                            ++contextual_bound_item_wins[
                                bound_type_index(decision.witness)];
                        );
                    }
                    if (decision.can_fathom) return false;
                }
                contribution_slot(item.id) = item.w;
                return true;
            },
            [&](detail::PointId state_index) {
            const detail::State& state = sequence.state(state_index);
            const int state_item_id = state.item_id;
            UKP_FULL_STATS(++slice.states_entered;);
            // PYAsUKP's transfert_in_sequence_result never fathoms a state
            // whose last item is b.  Preserving that chain is an invariant of
            // the threshold-based periodicity certificate and of fill_with_best.
            if (options_.use_bounds && state_item_id != periodic_best.id) {
                const detail::BoundDecision decision =
                    detail::evaluate_candidate(
                        ctx, state.weight, state.profit, inst.capacity,
                        incumbent, options_.bound_policy);
                record_bound_decision(decision);
                if (decision.lower_filter_hit) {
                    UKP_FULL_STATS(
                        ++result.stats.contextual_bound_calls_avoided_by_lower;
                        ++result.stats.contextual_bound_state_calls_avoided_by_lower;
                    );
                }
                if (decision.evaluated_mask != 0) {
                    UKP_BASIC_STATS(++result.stats.bound_calls;);
                    UKP_FULL_STATS(++result.stats.contextual_bound_state_queries;);
                    record_contextual_bound(decision, slice);
                    UKP_FULL_STATS(
                        ++contextual_bound_state_wins[
                            bound_type_index(decision.witness)];
                    );
                }
                if (decision.can_fathom) {
                    UKP_FULL_STATS(
                        if (decision.evaluated_mask != 0) {
                            ++contextual_bound_fathoms[
                                bound_type_index(decision.witness)];
                        }
                        ++slice.states_fathomed_by_bound;
                    );
                    UKP_BASIC_STATS(++result.stats.states_fathomed;);
                    return false;
                }
            }
            // Listing 1's fathoming improves z by greedily completing every
            // surviving optimal state with the current dominance-free items.
            consider_greedy_completion(state_index);
            // Every state exposed by the sequence is a strict skip-point.
            if (state_item_id >= 0) contribution_slot(state_item_id) = state.weight;
            UKP_BASIC_STATS(++result.stats.states_kept;);
            UKP_FULL_STATS(++slice.states_kept;);
            return true;
        });
        UKP_BASIC_STATS(
            result.stats.states_scanned += build.states_entered;
            result.stats.states_expanded += build.states_expanded;
            result.stats.successor_attempts += build.successor_attempts;
            result.stats.successor_item_scans += build.successor_item_scans;
            result.stats.backfill_attempts += build.backfill_attempts;
            result.stats.cursor_advances += build.cursor_advances;
            result.stats.items_considered_for_introduction +=
                build.items_considered_for_introduction;
            result.stats.items_introduced += build.items_introduced;
            result.stats.items_rejected_by_envelope += build.items_rejected_by_envelope;
            result.stats.items_rejected_by_bound += build.items_rejected_by_bound;
            result.stats.points_generated += build.points_generated;
            result.stats.dp_capacity_processed = yb;
        );
        UKP_FULL_STATS(
            result.stats.active_item_samples += build.active_item_samples;
            result.stats.active_items_sum += build.active_items_sum;
            result.stats.active_items_max = std::max(
                result.stats.active_items_max, build.active_items_max);
            for (std::size_t decile = 0;
                 decile < result.stats.items_introduced_by_capacity_decile.size();
                 ++decile) {
                result.stats.items_introduced_by_capacity_decile[decile] +=
                    build.items_introduced_by_capacity_decile[decile];
                result.stats.items_introduced_by_reduction_decile[decile] +=
                    build.items_introduced_by_reduction_decile[decile];
            }
        );
        residual_delta.had_item_decisions = next_item != previous_next_item;
        // Introduced items receive their initial contribution in the callback.
        // A considered item that still has no contribution was rejected by the
        // envelope or contextual bound and leaves the residual set.
        for (std::size_t index = previous_next_item; index < next_item; ++index) {
            const detail::ActiveItem& item = items_by_weight[index];
            if (contribution_slot(item.id) == kNoContribution) {
                residual_delta.request_removal(item.id);
            }
        }
        UKP_FULL_STATS(
            slice.successor_attempts += build.successor_attempts;
            slice.states_expanded += build.states_expanded;
            slice.successor_item_scans += build.successor_item_scans;
            slice.backfill_attempts += build.backfill_attempts;
            slice.cursor_advances += build.cursor_advances;
            slice.states_created += build.states_created;
            slice.items_considered_for_introduction +=
                build.items_considered_for_introduction;
            slice.items_introduced += build.items_introduced;
            slice.items_rejected_by_envelope += build.items_rejected_by_envelope;
            slice.items_rejected_by_bound += build.items_rejected_by_bound;
        );
        if (options_.use_bounds && incumbent >= global_bound.upper) {
            closed_by_bound = true;
            commit_residual_delta(false);
            UKP_FULL_STATS(
                slice.active_items_after = static_cast<long long>(active_items.size());
            );
            publish_slice_stats<kFullStats>(
                result.stats.slices, std::move(slice));
            break;
        }

        // Dynamic threshold dominance from EDUK/PYAsUKP. Record every
        // decision first; the transaction below updates membership, cursors,
        // active_items, suffix minima, and BoundContext exactly once.
        if (!active_items.empty()) {
            std::size_t remaining_active = active_items.size();
            for (const detail::ActiveItem& item : active_items) {
                const Weight contribution = contribution_slot(item.id);
                if (contribution == kNoContribution) {
                    throw std::logic_error("active item has no introduction contribution");
                }
                if (remaining_active > 1 &&
                    safe_add(contribution, item.w) <= yb) {
                    --remaining_active;
                    residual_delta.request_threshold_removal(item);
                    UKP_BASIC_STATS(++result.stats.items_removed_threshold;);
                    UKP_FULL_STATS(++slice.items_removed_threshold;);
                }
            }
        }

        // Active-item bound fathoming is intentionally disabled. A residual
        // BoundContext does not cover every historical continuation already
        // materialized in CriticalSequence, so using it to eliminate an
        // already-active item can underestimate that item's true remaining
        // potential. The unified bound engine remains enabled for the safe
        // call sites (initial reduction, item introduction, and state
        // fathoming).

        commit_residual_delta(true);
        UKP_FULL_STATS(
            slice.active_items_after = static_cast<long long>(active_items.size());
        );
        publish_slice_stats<kFullStats>(
            result.stats.slices, std::move(slice));
        const std::size_t active_count = active_items.size();
        // EDUK2 tests Chainlist.is_single only after reduction, i.e. after
        // Select.next_lightest has considered every item.  At that point a
        // singleton decreasingS containing b is the implementation's
        // threshold-dominance certificate for the paper's y+ level.
        const bool all_items_introduced = next_item == items_by_weight.size();
        if (options_.use_periodicity && all_items_introduced && active_count == 1 &&
            active_items.front().id == periodic_best.id) {
            periodicity_detected = true;
            UKP_BASIC_STATS(
                result.stats.periodicity_level = yb;
                ++result.stats.periodicity_hits;
                result.stats.active_items_at_periodicity =
                    static_cast<long long>(active_count);
            );

            // Direct counterpart of PYAsUKP's fill_with_best.  Select a
            // critical point in the capacity's residue class and complete it
            // with copies of b; no new DP states are required beyond y+.
            periodicity_base = sequence.stored_states() - 1;
            const detail::State& last_state = sequence.state(periodicity_base);
            const Weight difference = inst.capacity - last_state.weight;
            const Weight remainder = difference % periodic_best.w;
            periodicity_best_copies = difference / periodic_best.w;
            if (remainder != 0) {
                const Weight target_weight =
                    last_state.weight - (periodic_best.w - remainder);
                periodicity_base = sequence.state_at_or_before(target_weight);
                ++periodicity_best_copies;
            }
            const detail::State& base_state = sequence.state(periodicity_base);
            if (base_state.item_id == periodic_best.id) {
                periodicity_best_copies =
                    (inst.capacity - base_state.weight) / periodic_best.w;
            }
            break;
        }
        if (!half_capacity_extension_done && yb >= initial_process_limit) {
            const Weight active_wmax = active_items.empty() ? Weight{0} :
                std::max_element(active_items.begin(), active_items.end(),
                    [](const detail::ActiveItem& left,
                       const detail::ActiveItem& right) { return left.w < right.w; })->w;
            process_limit = std::min(inst.capacity, safe_add(yb, active_wmax));
            half_capacity_extension_done = true;
        }
        if (active_count == 1 && half_capacity_extension_done && yb >= process_limit) break;
        ya = yb;
    }

    dp_phase.stop();

    // Cursor suffixes still represented when the exact stopping certificate
    // fires are work the eager implementation had already materialized.
    // Publish per-item totals and the capacity-feasible deferred suffix.
    if constexpr (kFullStats) {
        long long final_historical_avoided = 0;
        for (const detail::ActiveItem& item : items_by_weight) {
            if (sequence.item_was_introduced(item)) {
                result.stats.backfill_attempts_by_item[
                    static_cast<std::size_t>(item.id)] =
                        sequence.item_backfill_attempts(item);
                final_historical_avoided += static_cast<long long>(
                    sequence.unprocessed_historical_states(item, candidate_limit));
            }
        }
        result.stats.historical_states_avoided += final_historical_avoided;
        if (!result.stats.slices.empty()) {
            result.stats.slices.back().historical_states_avoided +=
                final_historical_avoided;
        }
    }

    PhaseTimer<kFullStats> reconstruction_phase(
        result.stats.phase_reconstruction_ns);

    if (periodicity_detected) {
        const detail::State& base_state = sequence.state(periodicity_base);
        std::vector<long long> multiplicity(inst.items.size(), 0);
        for (detail::PointId cursor = periodicity_base; cursor != detail::no_point;
             cursor = sequence.state(cursor).predecessor) {
            const int item_id = sequence.state(cursor).item_id;
            if (item_id < 0) break;
            if (static_cast<std::size_t>(item_id) >= multiplicity.size()) {
                throw std::runtime_error("periodic backtracking failed");
            }
            ++multiplicity[static_cast<std::size_t>(item_id)];
        }
        if (periodic_best.id < 0 ||
            static_cast<std::size_t>(periodic_best.id) >= multiplicity.size()) {
            throw std::runtime_error("periodic best item is outside multiplicity");
        }
        multiplicity[static_cast<std::size_t>(periodic_best.id)] +=
            periodicity_best_copies;
        const Profit periodic_profit = safe_add(
            base_state.profit, safe_mul(periodicity_best_copies, periodic_best.p));
        const Weight periodic_weight = safe_add(
            base_state.weight, safe_mul(periodicity_best_copies, periodic_best.w));
        incumbent_solution.consider(periodic_profit, periodic_weight,
                                    std::move(multiplicity));

        reconstruction_phase.stop();
        publish_dp_telemetry();
        result.solution = incumbent_solution.solution("faithful");
        UKP_BASIC_STATS(
            result.stats.estimated_state_bytes =
                static_cast<long long>(sequence.estimated_bytes());
            result.stats.active_items_final = 1;
            result.stats.dp_stop_reason = "periodicity";
            result.stats.stop_reason = "periodicity";
        );
        return result;
    }

    // c/2 cut: the sequence query is the prefix maximum, because its profits
    // are strictly increasing with its skip-point weights.
    detail::PointId first_index = 0;
    detail::PointId second_index = 0;
    Profit split_profit = 0;
    const std::vector<detail::State>& states = sequence.states();
    std::size_t partner_position = states.size() - 1;
    for (detail::PointId index = 0; index < states.size(); ++index) {
        const detail::State& state = states[index];
        const Weight residual_capacity = inst.capacity - state.weight;
        // State weights increase while the complementary capacity decreases,
        // so the best feasible partner moves only toward the sequence front.
        while (partner_position > 0 &&
               states[partner_position].weight > residual_capacity) {
            --partner_position;
        }
        const detail::PointId partner = partner_position;
        const Profit candidate = safe_add(state.profit, states[partner].profit);
        if (candidate > split_profit) {
            split_profit = candidate;
            first_index = index;
            second_index = partner;
        }
    }

    if (split_profit <= incumbent_solution.profit) {
        reconstruction_phase.stop();
        publish_dp_telemetry();
        result.solution = incumbent_solution.solution("faithful");
        const std::size_t active_count = active_items.size();
        UKP_BASIC_STATS(
            result.stats.estimated_state_bytes =
                static_cast<long long>(sequence.estimated_bytes());
            result.stats.active_items_final = static_cast<long long>(active_count);
            result.stats.dp_stop_reason = closed_by_bound ? "bound_closed" :
                (active_count == 1 ? "single_active_item" : "half_capacity_cut");
            result.stats.stop_reason = closed_by_bound ? "dp_bound_closed" :
                (active_count == 1 ? "single_item" : "half_capacity");
        );
        return result;
    }

    Solution sol;
    sol.profit = split_profit;
    sol.weight = 0;
    sol.optimal = true;
    sol.solver_name = "faithful";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);

    // Solutions and the DP reconstruction are indexed by item ID.  Build the
    // direct lookup once, retaining the first matching item just as find_if
    // did when duplicate IDs are supplied.
    std::vector<Weight> item_weight_by_id(inst.items.size());
    std::vector<bool> item_id_present(inst.items.size(), false);
    for (const Item& item : inst.items) {
        if (item.id < 0 || static_cast<std::size_t>(item.id) >= item_weight_by_id.size()) {
            throw std::runtime_error("backtracking failed");
        }
        const std::size_t item_index = static_cast<std::size_t>(item.id);
        if (!item_id_present[item_index]) {
            item_weight_by_id[item_index] = item.w;
            item_id_present[item_index] = true;
        }
    }

    auto add_trace = [&](detail::PointId index) {
      while (index != detail::no_point) {
        const detail::State& state = sequence.state(index);
        const int item_id = state.item_id;
        if (item_id < 0) break;
        if (static_cast<std::size_t>(item_id) >= item_weight_by_id.size() ||
            !item_id_present[static_cast<std::size_t>(item_id)]) {
            throw std::runtime_error("backtracking failed");
        }
        sol.multiplicity_by_id[static_cast<std::size_t>(item_id)]++;
        sol.weight += item_weight_by_id[static_cast<std::size_t>(item_id)];
        index = state.predecessor;
      }
    };
    add_trace(first_index);
    add_trace(second_index);

    result.solution = std::move(sol);
    reconstruction_phase.stop();
    publish_dp_telemetry();
    const std::size_t active_count = active_items.size();
    UKP_BASIC_STATS(
        result.stats.estimated_state_bytes =
            static_cast<long long>(sequence.estimated_bytes());
        result.stats.active_items_final = static_cast<long long>(active_count);
        result.stats.dp_stop_reason = closed_by_bound ? "bound_closed" :
            (active_count == 1 ? "single_active_item" : "half_capacity_cut");
        result.stats.stop_reason = closed_by_bound ? "dp_bound_closed" :
            (active_count == 1 ? "single_item" : "half_capacity");
    );
    return result;
}

#undef UKP_FULL_STATS
#undef UKP_BASIC_STATS

}  // namespace ukp::faithful
