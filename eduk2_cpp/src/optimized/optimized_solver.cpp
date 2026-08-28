#include "ukp/optimized_solver.hpp"
#include "ukp/dominance.hpp"
#include "eduk2_bounds.hpp"
#include "critical_sequence.hpp"
#include "preprocessing.hpp"
#include "incumbent.hpp"
#include "instance_features.hpp"
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

namespace ukp::optimized {
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

const char* pyasukp_policy_name(BoundPolicy policy) {
    switch (policy) {
        case BoundPolicy::U3: return "MT";
        case BoundPolicy::V: return "V";
        case BoundPolicy::PyasukpBoth: return "Both";
        default: return "none";
    }
}

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
    long long branch_evaluations = 0;
    long long incumbent_improvements = 0;
    long long last_incumbent_improvement_node = 0;
    long long last_incumbent_improvement_work = 0;
    long long fractional_bound_calls = 0;
    long long fractional_bound_prunes = 0;
    long long u3_calls = 0;
    long long u3_prunes = 0;
    long long strong_calls = 0;
    long long strong_prunes = 0;
    bool stopped_for_work = false;
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
    bool use_fractional_bound;
    bool use_u3_bound;
    long long work_budget;
    CoreSearchResult result;
    std::unordered_map<Weight, Profit> best_profit_at_weight;
    std::vector<Weight> min_remaining_weight;
    std::vector<std::size_t> suffix_best_ratio;
    // PYAsUKP's default bandbukp2 path has bbnewv=false and therefore does
    // not use the Dynefflist state table.  Keep the memoization available for
    // experimental modes only; keying solely by used_weight is not a valid
    // equivalence relation across different B&B levels.
    bool use_state_memo = true;
};

std::vector<std::size_t> suffix_best_ratio(const std::vector<Item>& items) {
    std::vector<std::size_t> result(items.size(), 0);
    if (items.empty()) return result;
    result.back() = items.size() - 1;
    for (std::size_t i = items.size() - 1; i > 0; --i) {
        const std::size_t next = result[i];
        result[i - 1] = better_ratio(items[i - 1], items[next]) ? i - 1 : next;
    }
    return result;
}

bool work_budget_exceeded(const CoreTraversal& search) {
    if (search.work_budget <= 0 ||
        search.result.branch_evaluations < search.work_budget) return false;
    const long long since_improvement = search.result.branch_evaluations -
        search.result.last_incumbent_improvement_work;
    return since_improvement >= std::max<long long>(1, search.work_budget / 4) &&
           search.result.strong_calls >= std::max<long long>(1, search.work_budget / 8);
}

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
    if (!search.use_state_memo) return true;
    const auto known = search.best_profit_at_weight.find(used_weight);
    if (known != search.best_profit_at_weight.end() && known->second >= used_profit) return false;
    search.best_profit_at_weight[used_weight] = used_profit;
    return true;
}

void record_core_solution(CoreTraversal& search, Profit profit,
                          const std::vector<long long>& multiplicity) {
    if (profit > search.result.profit) {
        ++search.result.incumbent_improvements;
        search.result.last_incumbent_improvement_node = search.result.nodes;
        search.result.last_incumbent_improvement_work =
            search.result.branch_evaluations;
        search.result.profit = profit;
        search.result.multiplicity = multiplicity;
    }
    if (search.result.profit >= search.global_upper) search.result.closed = true;
}

void complete(CoreTraversal& search, std::size_t item_index, Weight used_weight,
              Profit used_profit, std::vector<long long>& multiplicity) {
    if (search.result.closed || search.result.stopped_for_work ||
        search.result.nodes >= search.limit) return;
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
        ++search.result.branch_evaluations;
        if (work_budget_exceeded(search)) {
            search.result.stopped_for_work = true;
            return;
        }
        const Weight next_weight = used_weight + safe_mul(count, item.w);
        const Profit next_profit = safe_add(used_profit, safe_mul(count, item.p));
        const Weight residual_capacity = search.capacity - next_weight;
        bool pruned = false;
        if (search.use_fractional_bound) {
            if constexpr (kBasicStats) ++search.result.fractional_bound_calls;
            Profit residual_upper = 0;
            if (item_index + 1 < search.items.size()) {
                const Item& best = search.items[search.suffix_best_ratio[item_index + 1]];
                residual_upper = floor_mul_div(residual_capacity, best.p, best.w);
            }
            if (safe_add(next_profit, residual_upper) <= search.result.profit) {
                if constexpr (kBasicStats) ++search.result.fractional_bound_prunes;
                pruned = true;
            }
        }
        if (!pruned && search.use_u3_bound) {
            if constexpr (kBasicStats) ++search.result.u3_calls;
            const Profit upper = compute_bound(
                search.bounds, residual_capacity, BoundPolicy::U3).upper;
            if (safe_add(next_profit, upper) <= search.result.profit) {
                if constexpr (kBasicStats) ++search.result.u3_prunes;
                pruned = true;
            }
        }
        if (!pruned) {
            ++search.result.strong_calls;
            const Profit upper = compute_bound(
                search.bounds, residual_capacity, search.policy).upper;
            if (safe_add(next_profit, upper) <= search.result.profit) {
                if constexpr (kBasicStats) ++search.result.strong_prunes;
                pruned = true;
            }
        }
        if (!pruned) {
            multiplicity[static_cast<std::size_t>(item.id)] = count;
            complete(search, item_index + 1, next_weight, next_profit, multiplicity);
            multiplicity[static_cast<std::size_t>(item.id)] = 0;
        }
        if (count == 0 || search.result.closed || search.result.stopped_for_work ||
            search.result.nodes >= search.limit) break;
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
                               Profit initial_incumbent,
                               const std::vector<long long>& initial_multiplicity,
                               const EffectiveOptions& effective, bool paper_faithful_mode,
                               BoundContextTelemetry* context_telemetry,
                               bool use_fractional_bound, bool use_u3_bound,
                               long long work_budget,
                               std::vector<int>* selected_core_ids = nullptr) {
    std::vector<Item> core_items;
    if (paper_faithful_mode) {
        // dp_items is already globally reduced and sorted by better_ratio.
        // The faithful core is exactly its prefix: no local ordering or
        // filtering is permitted on this path.
        const std::size_t n = dp_items.size();
        const std::size_t core_size = std::min(n, std::max<std::size_t>(500, n / 100));
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
    const bool include_optional_bounds =
        policy == BoundPolicy::BestCertified ||
        policy == BoundPolicy::TauStar ||
        policy == BoundPolicy::BestItemStar;
    BoundContext core_bounds = make_bound_context(
        filtered, context_telemetry, include_optional_bounds);
    CoreTraversal search{filtered, core_bounds, policy, capacity, global_upper,
                         std::max<long long>(0, limit), use_fractional_bound,
                         use_u3_bound, work_budget, {}, {}, lightest_worse(filtered),
                         suffix_best_ratio(filtered), !paper_faithful_mode};
    search.result.multiple_removed = core_multiple_removed;
    search.result.modular_removed = core_modular_removed;
    search.result.profit = initial_incumbent;
    search.result.multiplicity = initial_multiplicity;
    if (search.result.multiplicity.size() != original_count) {
        search.result.multiplicity.assign(original_count, 0);
    }
    std::vector<long long> multiplicity(original_count, 0);
    greedy_fill(search, multiplicity);
    backtrack(search, multiplicity);
    return search.result;
}


std::vector<const Item*> pyasukp_variable_reduction_order(
    const std::vector<Item>& items, Weight capacity) {
    // Prepro.ends_bests_others scans the input in increasing index order while
    // `place` conses every remaining item.  Init.structures then builds
    // `imin1 :: remains`, so after the minimum-weight item the candidates are
    // visited in reverse input order.  C++ item ids are assigned in input
    // order, allowing us to reproduce that order for the stage-1 survivors.
    const Item* lightest = nullptr;
    for (const Item& item : items) {
        if (item.w > capacity) continue;
        if (lightest == nullptr || item.w < lightest->w ||
            (item.w == lightest->w && item.p > lightest->p) ||
            (item.w == lightest->w && item.p == lightest->p &&
             item.id < lightest->id)) {
            lightest = &item;
        }
    }

    std::vector<const Item*> order;
    order.reserve(items.size());
    if (lightest != nullptr) order.push_back(lightest);

    std::vector<const Item*> remaining;
    remaining.reserve(items.size());
    for (const Item& item : items) {
        if (item.w <= capacity && &item != lightest) remaining.push_back(&item);
    }
    std::sort(remaining.begin(), remaining.end(),
              [](const Item* left, const Item* right) {
                  return left->id > right->id;
              });
    order.insert(order.end(), remaining.begin(), remaining.end());
    return order;
}

std::vector<Item> reduce_variables_with_pyasukp_incumbent(
    const Instance& inst,
    const std::vector<Item>& ratio_ordered_items,
    const BoundContext& immutable_bound_context,
    Weight capacity,
    Profit& incumbent,
    detail::Incumbent& incumbent_solution,
    long long& bound_calls,
    BoundPolicy resolved_pyasukp_policy,
    detail::BoundDecisionTelemetry* decision_telemetry) {
    if (resolved_pyasukp_policy != BoundPolicy::U3 &&
        resolved_pyasukp_policy != BoundPolicy::V &&
        resolved_pyasukp_policy != BoundPolicy::PyasukpBoth) {
        throw std::invalid_argument(
            "PYAsUKP variable reduction requires resolved MT/V/Both policy");
    }

    std::vector<unsigned char> keep(inst.items.size(), 0);
    for (const Item& item : ratio_ordered_items) {
        if (item.id < 0 || static_cast<std::size_t>(item.id) >= keep.size()) {
            throw std::logic_error("item id is outside variable-reduction table");
        }
        if (item.w <= capacity || item.id == immutable_bound_context.best.id) {
            keep[static_cast<std::size_t>(item.id)] = 1;
        }
    }

    const std::vector<const Item*> reduction_order =
        pyasukp_variable_reduction_order(ratio_ordered_items, capacity);
    for (const Item* item_ptr : reduction_order) {
        const Item& item = *item_ptr;
        if (item.id == immutable_bound_context.best.id) {
            // The initial incumbent already contains the maximal number of b1.
            continue;
        }

        // Bounds.with_wp first builds a feasible residual completion and uses
        // it to strengthen bound.z.  Only then does Init.structures call
        // is_context_dominated.  The context itself remains the original,
        // pre-reduction bound exactly as in PYAsUKP.
        const Weight residual_capacity = capacity - item.w;
        const detail::FeasibleCompletion completion = detail::complete_with_bound(
            immutable_bound_context, resolved_pyasukp_policy, residual_capacity);
        const Profit candidate_profit = safe_add(item.p, completion.profit);
        const Weight candidate_weight = safe_add(item.w, completion.weight);

        if (candidate_profit > incumbent_solution.profit ||
            (candidate_profit == incumbent_solution.profit &&
             candidate_weight > incumbent_solution.weight)) {
            std::vector<long long> multiplicity(inst.items.size(), 0);
            multiplicity[static_cast<std::size_t>(item.id)] = 1;
            for (std::size_t index = 0; index < completion.term_count; ++index) {
                const detail::CompletionTerm& term = completion.terms[index];
                if (term.item_id < 0 ||
                    static_cast<std::size_t>(term.item_id) >= multiplicity.size()) {
                    throw std::logic_error(
                        "bound completion item id is outside multiplicity");
                }
                multiplicity[static_cast<std::size_t>(term.item_id)] += term.count;
            }
            if (incumbent_solution.consider(candidate_profit, candidate_weight,
                                            std::move(multiplicity))) {
                incumbent = incumbent_solution.profit;
            }
        }

        const detail::BoundDecision decision = detail::evaluate_candidate(
            immutable_bound_context, item.w, item.p, capacity, incumbent,
            resolved_pyasukp_policy);
        detail::accumulate_bound_decision_telemetry(decision_telemetry, decision);
        if (decision.evaluated_mask != 0) ++bound_calls;
        if (decision.can_fathom) {
            keep[static_cast<std::size_t>(item.id)] = 0;
        }
    }

    // The DP relies on stable better_ratio order.  Evaluate candidates in the
    // PYAsUKP preprocessing order above, but publish survivors in the original
    // ratio order used by the C++ CriticalSequence.
    std::vector<Item> reduced;
    reduced.reserve(ratio_ordered_items.size());
    for (const Item& item : ratio_ordered_items) {
        if (item.id == immutable_bound_context.best.id ||
            keep[static_cast<std::size_t>(item.id)] != 0) {
            reduced.push_back(item);
        }
    }
    return reduced;
}

Solution solution_from_best_item(const Instance& inst, const Item& best, long long count) {
    Solution sol;
    sol.profit = safe_mul(count, best.p);
    sol.weight = count * best.w;
    sol.optimal = true;
    sol.solver_name = "optimized";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);
    if (best.id >= 0 && static_cast<std::size_t>(best.id) < sol.multiplicity_by_id.size()) {
        sol.multiplicity_by_id[static_cast<std::size_t>(best.id)] = count;
    }
    return sol;
}

struct CheapIncumbentResult {
    Profit profit = 0;
    Weight weight = 0;
    std::vector<long long> multiplicity;
    long long candidates_tested = 0;
};

CheapIncumbentResult cheap_incumbent(const std::vector<Item>& ratio_ordered_items,
                                     Weight capacity, std::size_t original_count,
                                     int requested_top_k) {
    CheapIncumbentResult best;
    best.multiplicity.assign(original_count, 0);
    if (ratio_ordered_items.empty() || requested_top_k <= 0) return best;
    const std::size_t top_k = std::min<std::size_t>(
        ratio_ordered_items.size(), static_cast<std::size_t>(requested_top_k));

    auto consider = [&](const Item* seeded_item, long long seeded_count,
                        const Item& fill_item) {
        Weight weight = seeded_item == nullptr ? 0 : safe_mul(seeded_count, seeded_item->w);
        Profit profit = seeded_item == nullptr ? 0 : safe_mul(seeded_count, seeded_item->p);
        if (weight > capacity) return;
        const long long fill_count = (capacity - weight) / fill_item.w;
        weight = safe_add(weight, safe_mul(fill_count, fill_item.w));
        profit = safe_add(profit, safe_mul(fill_count, fill_item.p));
        ++best.candidates_tested;
        if (profit <= best.profit) return;
        std::fill(best.multiplicity.begin(), best.multiplicity.end(), 0);
        if (seeded_item != nullptr) {
            best.multiplicity[static_cast<std::size_t>(seeded_item->id)] += seeded_count;
        }
        best.multiplicity[static_cast<std::size_t>(fill_item.id)] += fill_count;
        best.profit = profit;
        best.weight = weight;
    };

    for (std::size_t fill = 0; fill < top_k; ++fill) {
        consider(nullptr, 0, ratio_ordered_items[fill]);
    }
    for (std::size_t seed = 1; seed < top_k; ++seed) {
        const Item& seeded_item = ratio_ordered_items[seed];
        const long long max_seed = std::min<long long>(4, capacity / seeded_item.w);
        for (long long count = 1; count <= max_seed; ++count) {
            for (std::size_t fill = 0; fill < top_k; ++fill) {
                consider(&seeded_item, count, ratio_ordered_items[fill]);
            }
        }
    }
    return best;
}

}  // namespace

Solver::Solver(SolverOptions options) : options_(options) {}

SolverResult Solver::solve(const Instance& inst) {
    if (inst.capacity < 0) throw std::invalid_argument("negative capacity");
    SolverResult result;
    const EffectiveOptions effective = effective_options(options_);
    UKP_BASIC_STATS(
        result.stats.original_items = static_cast<long long>(inst.items.size());
        result.stats.selected_kernel = "eduk2";
        result.stats.dispatch_reason = "dispatcher_not_enabled";
    );
    UKP_FULL_STATS(
        result.stats.backfill_attempts_by_item.assign(inst.items.size(), -1);
    );
    if (inst.items.empty() || inst.capacity == 0) {
        result.solution.multiplicity_by_id.assign(inst.items.size(), 0);
        result.solution.optimal = true;
        result.solution.solver_name = "optimized";
        UKP_BASIC_STATS(
            result.stats.stop_reason = "empty_instance";
            result.stats.dp_stop_reason = "empty_instance";
        );
        return result;
    }

    std::vector<Item> common_items;
    {
        PhaseTimer<kFullStats> phase(result.stats.phase_common_preprocessing_ns);
        common_items = detail::common_preprocess_items(inst);
    }
    if constexpr (kFullStats) {
        PhaseTimer<true> phase(result.stats.phase_feature_extraction_ns);
        result.stats.instance_features =
            detail::extract_instance_features(inst, common_items);
    }
    if (common_items.empty()) {
        result.solution.multiplicity_by_id.assign(inst.items.size(), 0);
        result.solution.optimal = true;
        result.solution.solver_name = "optimized";
        UKP_BASIC_STATS(
            result.stats.after_preprocess_items = 0;
            result.stats.stop_reason = "no_feasible_items";
            result.stats.dp_stop_reason = "not_started";
        );
        return result;
    }

    detail::PreprocessResult preprocessing;
    {
        PhaseTimer<kFullStats> phase(result.stats.phase_preprocessing_ns);
        preprocessing = detail::preprocess_items_for_eduk2(
            common_items, effective.simple_dominance);
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
            result.stats.bound_context_incremental_updates =
                context_telemetry.incremental_updates;
            result.stats.bound_context_items_processed = context_telemetry.items_processed;
            result.stats.phase_context_maintenance_ns =
                context_telemetry.incremental_maintenance_ns;
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
    BoundPolicy effective_bound_policy = options_.bound_policy;
    BoundPolicy pyasukp_completion_policy = BoundPolicy::U3;
    BoundContext pyasukp_completion_bound_ctx;
    const BoundContext* bound_completion_context = nullptr;
    const bool use_bound_completion =
        options_.use_bounds && options_.use_pyasukp_bound_completion;
    if (options_.use_bounds) {
        // initialize_bounds is the only initial BoundContext construction on
        // the bounded path.  The previous code built the same context twice.
        detail::BoundPhase bound_phase = detail::initialize_bounds(
            items, inst.capacity, options_.bound_policy, context_telemetry_ptr);
        ctx = std::move(bound_phase.context);
        global_bound = bound_phase.global;
        best_count = bound_phase.best_count;
        incumbent = bound_phase.incumbent;
        effective_bound_policy = bound_phase.effective_policy;
        if (options_.bound_policy == BoundPolicy::PyasukpFaithful) {
            pyasukp_completion_policy = effective_bound_policy;
            UKP_BASIC_STATS(
                result.stats.pyasukp_bound_mode =
                    pyasukp_policy_name(pyasukp_completion_policy);
            );
        }
        if (use_bound_completion &&
            options_.bound_policy != BoundPolicy::PyasukpFaithful) {
            // Configuration B keeps BestCertified for upper bounds, but its
            // incumbent completion must use the minimum-weight V parameters
            // from PYAsUKP rather than the generic C++ V/Tau* context.
            pyasukp_completion_bound_ctx =
                make_bound_context(items, nullptr, false);
            pyasukp_completion_policy = resolve_pyasukp_policy(
                pyasukp_completion_bound_ctx, inst.capacity);
            UKP_BASIC_STATS(
                result.stats.pyasukp_bound_mode =
                    pyasukp_policy_name(pyasukp_completion_policy);
            );
        }
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

    // PYAsUKP builds its context-dominance bound before removing context-
    // dominated variables and reuses that immutable bound for every later item
    // introduction.  Keep the same pre-reduction context as a correctness guard
    // for irreversible pruning decisions made by the shrinking residual context.
    BoundContext introduction_bound_ctx;
    if (options_.use_bounds) introduction_bound_ctx = ctx;
    if (use_bound_completion) {
        bound_completion_context =
            options_.bound_policy == BoundPolicy::PyasukpFaithful
                ? &introduction_bound_ctx
                : &pyasukp_completion_bound_ctx;
    }

    if (options_.use_bounds) {
        const long long items_before_bound_reduction = static_cast<long long>(items.size());
#ifndef NDEBUG
        assert(std::is_sorted(items.begin(), items.end(), better_ratio));
#endif
        if (options_.bound_policy == BoundPolicy::PyasukpFaithful &&
            use_bound_completion) {
            items = reduce_variables_with_pyasukp_incumbent(
                inst, items, introduction_bound_ctx, inst.capacity, incumbent,
                incumbent_solution, bound_calls_counter, effective_bound_policy,
                bound_decision_telemetry_ptr);
        } else {
            items = detail::reduce_variables_by_bound(
                items, ctx, inst.capacity, incumbent, bound_calls_counter,
                effective_bound_policy, bound_decision_telemetry_ptr);
        }
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

        // Bounds.with_wp raises Optimal as soon as its feasible z reaches the
        // original certified upper bound.  The incumbent stored above is fully
        // reconstructible, so the C++ faithful path can close at the same point.
        if (incumbent >= global_bound.upper) {
            global_bounds_phase.stop();
            result.solution = incumbent_solution.solution("optimized");
            UKP_BASIC_STATS(
                result.stats.active_items_final = static_cast<long long>(items.size());
                result.stats.stop_reason = "preprocessing_bound";
                result.stats.dp_stop_reason = "not_started";
            );
            publish_context_telemetry();
            return result;
        }
    }
    global_bounds_phase.stop();

    UKP_BASIC_STATS(result.stats.incumbent_before_cheap_heuristic = incumbent;);
    if (options_.use_cheap_incumbent) {
        PhaseTimer<kFullStats> cheap_phase(result.stats.phase_cheap_incumbent_ns);
        CheapIncumbentResult cheap = cheap_incumbent(
            items, inst.capacity, inst.items.size(),
            std::max(0, std::min(16, options_.cheap_incumbent_top_k)));
        UKP_BASIC_STATS(
            result.stats.cheap_incumbent_candidates_tested = cheap.candidates_tested;
        );
        if (cheap.profit > incumbent) {
            incumbent = cheap.profit;
            incumbent_solution.consider(cheap.profit, cheap.weight,
                                         std::move(cheap.multiplicity));
        }
    }
    UKP_BASIC_STATS(result.stats.incumbent_after_cheap_heuristic = incumbent;);
    if (options_.use_bounds && incumbent >= global_bound.upper) {
        result.solution = incumbent_solution.solution("optimized");
        UKP_BASIC_STATS(
            result.stats.active_items_final = static_cast<long long>(items.size());
            result.stats.stop_reason = "cheap_incumbent_bound";
            result.stats.dp_stop_reason = "not_started";
        );
        publish_context_telemetry();
        return result;
    }

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
    // envelope/bound rejection or threshold removal. Keep membership by id for
    // duplicate/removal accounting; BoundContext itself now applies the same
    // removals incrementally while preserving dp_items' stable ratio order.
    std::vector<unsigned char> residual_item_alive(inst.items.size(), 0);
    auto residual_slot = [&](const auto& item) -> unsigned char& {
        if (item.id < 0 || static_cast<std::size_t>(item.id) >= residual_item_alive.size()) {
            throw std::logic_error("item id is outside residual bound membership");
        }
        return residual_item_alive[static_cast<std::size_t>(item.id)];
    };
    for (const Item& item : dp_items) residual_slot(item) = 1;
    if (options_.use_core_bb) {
        PhaseTimer<kFullStats> core_bb_phase(result.stats.phase_core_bb_ns);
        constexpr long long kFaithfulCoreNodeLimit = 10'000;
        const long long core_node_limit = options_.paper_faithful_mode
            ? kFaithfulCoreNodeLimit : options_.bb_node_limit;
        UKP_BASIC_STATS(result.stats.core_node_limit = core_node_limit;);
        std::vector<int>* selected_core_ids = nullptr;
        if constexpr (kFullStats) selected_core_ids = &result.stats.core_item_ids;
        UKP_BASIC_STATS(result.stats.bb_initial_incumbent = incumbent;);
        CoreSearchResult core = traverse_core(dp_items, ctx, effective_bound_policy, inst.capacity,
                                               global_bound.upper, options_.core_size,
                                               core_node_limit, inst.items.size(), incumbent,
                                               incumbent_solution.multiplicity_by_id, effective,
                                               options_.paper_faithful_mode, context_telemetry_ptr,
                                               options_.use_bb_fractional_bound,
                                               options_.use_bb_u3_bound,
                                               options_.bb_work_budget,
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
            result.stats.bb_branch_evaluations += core.branch_evaluations;
            result.stats.bb_incumbent_improvements += core.incumbent_improvements;
            result.stats.bb_last_incumbent_improvement_node =
                core.last_incumbent_improvement_node;
            result.stats.bb_final_incumbent = incumbent;
            result.stats.bb_fractional_bound_calls += core.fractional_bound_calls;
            result.stats.bb_fractional_bound_prunes += core.fractional_bound_prunes;
            result.stats.bb_u3_calls += core.u3_calls;
            result.stats.bb_u3_prunes += core.u3_prunes;
            result.stats.bb_strong_bound_calls += core.strong_calls;
            result.stats.bb_strong_bound_prunes += core.strong_prunes;
            if (core.stopped_for_work) ++result.stats.bb_work_stops;
            result.stats.items_removed_core_multiple += core.multiple_removed;
            result.stats.items_removed_modular += core.modular_removed;
        );
        if (core.closed) {
            result.solution = incumbent_solution.solution("optimized");
            UKP_BASIC_STATS(
                result.stats.active_items_final = static_cast<long long>(dp_items.size());
                result.stats.stop_reason = "core_bound_closed";
            );
            publish_context_telemetry();
            return result;
        }
    }

    // State fathoming must use the immutable global post-reduction context.
    // Threshold dominance shrinks `ctx` during the DP, but states already
    // materialized in CriticalSequence can still contain items removed from
    // that residual context and can later participate in the c/2 split.
    // Using the shrinking context here can therefore underestimate a state's
    // remaining potential and incorrectly suppress descendants needed by the
    // final historical-state reconstruction.
    const BoundContext state_bound_ctx = ctx;

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
        if (use_bound_completion) {
            active_suffix_min_weight.clear();
            return;
        }
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
        UKP_BASIC_STATS(++result.stats.greedy_completion_calls;);
        ensure_active_suffix_min_weight();
        const detail::State& state = sequence.state(state_index);
        Weight used_weight = state.weight;
        Weight remaining = inst.capacity - used_weight;
        Profit candidate_profit = state.profit;
        for (std::size_t index = 0; index < active_items.size(); ++index) {
            UKP_BASIC_STATS(++result.stats.greedy_completion_item_scans;);
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
            UKP_BASIC_STATS(++result.stats.greedy_completion_reconstruction_steps;);
            long long& count = reconstruction_multiplicity[static_cast<std::size_t>(item_id)];
            if (count == 0) reconstruction_touched_ids.push_back(item_id);
            ++count;
        }
        Weight reconstruction_weight = state.weight;
        Weight reconstruction_remaining = inst.capacity - reconstruction_weight;
        for (std::size_t index = 0; index < active_items.size(); ++index) {
            UKP_BASIC_STATS(++result.stats.greedy_completion_reconstruction_steps;);
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
            UKP_BASIC_STATS(
                ++result.stats.incumbent_improvements_dp;
                ++result.stats.greedy_completion_improvements;
            );
        }
        for (const int item_id : reconstruction_touched_ids) {
            reconstruction_multiplicity[static_cast<std::size_t>(item_id)] = 0;
        }
        reconstruction_touched_ids.clear();
    };

    auto consider_bound_completion = [&](detail::PointId state_index) {
        UKP_BASIC_STATS(
            ++result.stats.bound_completion_calls;
            switch (pyasukp_completion_policy) {
                case BoundPolicy::U3:
                    ++result.stats.bound_completion_u3_calls;
                    break;
                case BoundPolicy::V:
                    ++result.stats.bound_completion_v_calls;
                    break;
                case BoundPolicy::PyasukpBoth:
                    ++result.stats.bound_completion_both_calls;
                    break;
                default:
                    break;
            }
        );

        const detail::State& state = sequence.state(state_index);
        const Weight residual_capacity = inst.capacity - state.weight;
        if (bound_completion_context == nullptr) {
            throw std::logic_error("bound completion context was not initialized");
        }
        const detail::FeasibleCompletion completion =
            detail::complete_with_bound(
                *bound_completion_context, pyasukp_completion_policy,
                residual_capacity);
        const Profit candidate_profit =
            safe_add(state.profit, completion.profit);
        const Weight candidate_weight =
            safe_add(state.weight, completion.weight);

        if (candidate_profit < incumbent_solution.profit ||
            (candidate_profit == incumbent_solution.profit &&
             candidate_weight <= incumbent_solution.weight)) {
            return;
        }

        if (reconstruction_multiplicity.empty()) {
            reconstruction_multiplicity.assign(inst.items.size(), 0);
        }
        for (detail::PointId cursor = state_index; cursor != detail::no_point;
             cursor = sequence.state(cursor).predecessor) {
            const int item_id = sequence.state(cursor).item_id;
            if (item_id < 0) break;
            UKP_BASIC_STATS(++result.stats.bound_completion_reconstruction_steps;);
            long long& count =
                reconstruction_multiplicity[static_cast<std::size_t>(item_id)];
            if (count == 0) reconstruction_touched_ids.push_back(item_id);
            ++count;
        }
        for (std::size_t index = 0; index < completion.term_count; ++index) {
            const detail::CompletionTerm& term = completion.terms[index];
            if (term.count <= 0) continue;
            if (term.item_id < 0 ||
                static_cast<std::size_t>(term.item_id) >=
                    reconstruction_multiplicity.size()) {
                throw std::logic_error(
                    "bound completion item id is outside multiplicity");
            }
            UKP_BASIC_STATS(++result.stats.bound_completion_reconstruction_steps;);
            long long& count = reconstruction_multiplicity[
                static_cast<std::size_t>(term.item_id)];
            if (count == 0) reconstruction_touched_ids.push_back(term.item_id);
            count += term.count;
        }

        if (incumbent_solution.consider_sparse(
                candidate_profit, candidate_weight,
                reconstruction_multiplicity, reconstruction_touched_ids,
                state_index)) {
            incumbent = incumbent_solution.profit;
            residual_delta.incumbent_changed = true;
            UKP_BASIC_STATS(
                ++result.stats.incumbent_improvements_dp;
                ++result.stats.bound_completion_improvements;
            );
        }
        for (const int item_id : reconstruction_touched_ids) {
            reconstruction_multiplicity[static_cast<std::size_t>(item_id)] = 0;
        }
        reconstruction_touched_ids.clear();
    };

    // PYAsUKP switches Init.rwith_wp to Bounds.bound_up_half_c once c' has
    // been reached.  From that point on, every residual capacity c-y is in
    // the already solved prefix, so sequence_result gives an exact residual
    // completion rather than an MT/V estimate.
    auto consider_half_capacity_completion = [&](detail::PointId state_index) {
        const detail::State& state = sequence.state(state_index);
        const Weight residual_capacity = inst.capacity - state.weight;
        const detail::PointId partner_index =
            sequence.expandable_state_at_or_before(residual_capacity);
        const detail::State& partner = sequence.state(partner_index);
        const Profit candidate_profit = safe_add(state.profit, partner.profit);
        const Weight candidate_weight = safe_add(state.weight, partner.weight);

        if (candidate_profit < incumbent_solution.profit ||
            (candidate_profit == incumbent_solution.profit &&
             candidate_weight <= incumbent_solution.weight)) {
            return candidate_profit;
        }

        if (reconstruction_multiplicity.empty()) {
            reconstruction_multiplicity.assign(inst.items.size(), 0);
        }
        auto add_chain = [&](detail::PointId cursor) {
            for (; cursor != detail::no_point;
                 cursor = sequence.state(cursor).predecessor) {
                const int item_id = sequence.state(cursor).item_id;
                if (item_id < 0) break;
                long long& count = reconstruction_multiplicity[
                    static_cast<std::size_t>(item_id)];
                if (count == 0) reconstruction_touched_ids.push_back(item_id);
                ++count;
            }
        };
        add_chain(state_index);
        add_chain(partner_index);

        if (incumbent_solution.consider_sparse(
                candidate_profit, candidate_weight,
                reconstruction_multiplicity, reconstruction_touched_ids,
                state_index)) {
            incumbent = incumbent_solution.profit;
            residual_delta.incumbent_changed = true;
            UKP_BASIC_STATS(++result.stats.incumbent_improvements_dp;);
        }
        for (const int item_id : reconstruction_touched_ids) {
            reconstruction_multiplicity[static_cast<std::size_t>(item_id)] = 0;
        }
        reconstruction_touched_ids.clear();
        return candidate_profit;
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

        const std::size_t previous_context_size = ctx.items.size();
        apply_bound_context_removals(
            ctx, residual_delta.removed_ids,
            residual_delta.removal_requested_by_id, context_telemetry_ptr);
        if (previous_context_size - ctx.items.size() !=
            static_cast<std::size_t>(removed_now)) {
            throw std::logic_error(
                "incremental BoundContext removed an unexpected number of items");
        }
#ifndef NDEBUG
        verify_bound_context_against_full_rebuild(ctx);
#else
        if constexpr (kFullStats) {
            // Full telemetry keeps the same oracle used during development,
            // but benchmark builds compile this branch away entirely.
            verify_bound_context_against_full_rebuild(ctx);
        }
#endif
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
                    // The residual BoundContext can be smaller than the set of
                    // continuations already materialized in CriticalSequence.
                    // Before allowing that context to reject a newly introduced
                    // item, preserve the item whenever it can already combine
                    // with a historical DP state to improve the incumbent.
                    //
                    // This is a feasible lower-bound witness, not a new pruning
                    // rule: if item.p + f(c - item.w) > incumbent, no valid upper
                    // bound may fathom the item at this introduction point.
                    const Weight historical_residual = inst.capacity - item.w;
                    const Profit historical_feasible = safe_add(
                        item.p, sequence.value_at(historical_residual));
                    if (historical_feasible > incumbent) {
                        contribution_slot(item.id) = item.w;
                        return true;
                    }

                    const detail::BoundDecision decision =
                        detail::evaluate_candidate(
                            ctx, item.w, item.p, inst.capacity, incumbent,
                            effective_bound_policy);
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
                    if (decision.can_fathom) {
                        // `ctx` is only the residual active context.  Threshold
                        // dominance and earlier introduction decisions can remove
                        // variables from it even though a not-yet-introduced item
                        // may still need continuations represented by those
                        // variables in the original EDUK recurrence.  Therefore a
                        // residual-context fathom is only provisional.
                        //
                        // Recheck only prospective rejections against the immutable
                        // pre-reduction context, matching PYAsUKP's `bound` lifetime.
                        // The residual context remains the fast first-stage filter;
                        // the item is rejected only if the stable context independently
                        // certifies the same pruning decision.
                        const detail::BoundDecision stable_decision =
                            detail::evaluate_candidate(
                                introduction_bound_ctx, item.w, item.p,
                                inst.capacity, incumbent, effective_bound_policy);
                        record_bound_decision(stable_decision);
                        if (stable_decision.lower_filter_hit) {
                            UKP_FULL_STATS(
                                ++result.stats.contextual_bound_calls_avoided_by_lower;
                                ++result.stats.contextual_bound_item_calls_avoided_by_lower;
                            );
                        }
                        if (stable_decision.evaluated_mask != 0) {
                            UKP_BASIC_STATS(++result.stats.bound_calls;);
                            UKP_FULL_STATS(++result.stats.contextual_bound_item_queries;);
                            record_contextual_bound(stable_decision, slice);
                            UKP_FULL_STATS(
                                ++contextual_bound_item_wins[
                                    bound_type_index(stable_decision.witness)];
                            );
                        }
                        if (stable_decision.can_fathom) return false;
                    }
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
            if (options_.use_bounds && state_item_id != periodic_best.id &&
                options_.paper_faithful_mode && half_capacity_extension_done) {
                const Profit exact_completion =
                    consider_half_capacity_completion(state_index);
                // Bounds.is_context_dominated uses a strict comparison
                // (u < z).  bound_up_half_c has u == z for this state, so an
                // equal completion is kept exactly as in PYAsUKP.
                if (exact_completion < incumbent) {
                    UKP_BASIC_STATS(++result.stats.states_fathomed;);
                    UKP_FULL_STATS(++slice.states_fathomed_by_bound;);
                    return false;
                }
            } else if (options_.use_bounds && state_item_id != periodic_best.id) {
                const detail::BoundDecision decision =
                    detail::evaluate_candidate(
                        state_bound_ctx, state.weight, state.profit, inst.capacity,
                        incumbent, effective_bound_policy);
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
            // Change B replaces the later C++ greedy active-item scan with the
            // O(1) feasible completion attached to PYAsUKP's selected bound.
            // The switch is explicit so A/C retain the original baseline and
            // B/BC differ only in incumbent completion.
            if (!(options_.paper_faithful_mode && half_capacity_extension_done)) {
                if (use_bound_completion) {
                    consider_bound_completion(state_index);
                } else {
                    consider_greedy_completion(state_index);
                }
            }
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
        // potential. Item introduction keeps its historical feasible guard,
        // while state fathoming uses the immutable global `state_bound_ctx`.

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
        result.solution = incumbent_solution.solution("optimized");
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
        result.solution = incumbent_solution.solution("optimized");
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
    sol.solver_name = "optimized";
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

}  // namespace ukp::optimized
