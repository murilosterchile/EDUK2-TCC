#include "ukp/optimized_solver.hpp"
#include "ukp/bounds.hpp"
#include "ukp/dominance.hpp"
#include "core_branch_and_bound.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <cstdint>
#include <queue>
#include <cstdlib>
#include <iostream>
#include <functional>

namespace ukp::optimized {
namespace {

#if 0  // Moved to core_branch_and_bound.cpp; retained temporarily for reference.

struct CoreBBResult {
    Profit profit = 0;
    Weight weight = 0;
    std::vector<long long> multiplicity_by_id;
    long long nodes = 0;
    bool hit_limit = false;
    bool closed_gap = false;
};

struct CoreBBState {
    const std::vector<Item>* core = nullptr;
    Weight capacity = 0;
    long long node_limit = 0;
    long long nodes = 0;
    Profit best_profit = 0;
    Weight best_weight = 0;
    Profit target_upper = std::numeric_limits<Profit>::max();
    std::vector<long long> current;
    std::vector<long long> best;
    bool hit_limit = false;
    bool closed_gap = false;
};

Profit fractional_tail_bound(const std::vector<Item>& core, std::size_t k, Weight remaining) {
    if (remaining <= 0 || k >= core.size()) return 0;
    const Item& it = core[k];
    return floor_mul_div(remaining, it.p, it.w);
}

void core_bb_dfs(CoreBBState& st, std::size_t k, Weight used_w, Profit used_p) {
    if (st.closed_gap) return;
    if (st.nodes >= st.node_limit) {
        st.hit_limit = true;
        return;
    }
    ++st.nodes;

    if (used_p > st.best_profit || (used_p == st.best_profit && used_w > st.best_weight)) {
        st.best_profit = used_p;
        st.best_weight = used_w;
        st.best = st.current;
        if (st.best_profit >= st.target_upper) {
            st.closed_gap = true;
            return;
        }
    }

    if (k >= st.core->size()) return;

    const auto& core = *st.core;
    const Item& it = core[k];
    const Weight remaining = st.capacity - used_w;
    if (remaining <= 0) return;

    const long long max_x = remaining / it.w;

    // Explore high multiplicities first.  For favorable UKP instances this often
    // closes the upper bound in only a few nodes; for unfavorable instances the
    // adaptive node budget below prevents this search from dominating runtime.
    for (long long x = max_x; x >= 0; --x) {
        if (st.closed_gap) return;
        if (st.nodes >= st.node_limit) {
            st.hit_limit = true;
            return;
        }

        const Weight nw = used_w + x * it.w;
        const Profit np = safe_add(used_p, safe_mul(x, it.p));
        const Weight rem = st.capacity - nw;

        Profit optimistic = np;
        if (k + 1 < core.size()) {
            optimistic = safe_add(optimistic, fractional_tail_bound(core, k + 1, rem));
        }
        if (optimistic < st.best_profit) {
            if (x == 0) break;
            continue;
        }

        st.current[k] = x;
        if (np > st.best_profit || (np == st.best_profit && nw > st.best_weight)) {
            st.best_profit = np;
            st.best_weight = nw;
            st.best = st.current;
            if (st.best_profit >= st.target_upper) {
                st.closed_gap = true;
                st.current[k] = 0;
                return;
            }
        }
        if (k + 1 < core.size()) {
            core_bb_dfs(st, k + 1, nw, np);
        }
        st.current[k] = 0;

        if (x == 0) break;
    }
}

CoreBBResult run_core_bb(std::vector<Item> items,
                         Weight capacity,
                         long long node_limit,
                         int requested_core_size,
                         Profit incumbent,
                         Profit global_upper,
                         std::size_t original_item_count) {
    CoreBBResult out;
    out.multiplicity_by_id.assign(original_item_count, 0);
    if (items.empty() || node_limit <= 0) return out;

    std::sort(items.begin(), items.end(), better_ratio);

    int core_size = requested_core_size > 0
        ? requested_core_size
        : std::min<int>(static_cast<int>(items.size()), 48);
    core_size = std::max(1, std::min<int>(core_size, static_cast<int>(items.size())));
    items.resize(static_cast<std::size_t>(core_size));

    CoreBBState st;
    st.core = &items;
    st.capacity = capacity;
    st.node_limit = node_limit;
    st.best_profit = incumbent;
    st.target_upper = global_upper;
    st.current.assign(items.size(), 0);
    st.best.assign(items.size(), 0);

    // Greedy incumbent from the best ratio item, if it improves the provided one.
    const Item& best = items.front();
    const long long xb = capacity / best.w;
    const Profit greedy_profit = safe_mul(xb, best.p);
    if (greedy_profit > st.best_profit) {
        st.best_profit = greedy_profit;
        st.best_weight = xb * best.w;
        st.best[0] = xb;
    }
    if (st.best_profit >= st.target_upper) {
        st.closed_gap = true;
    } else {
        core_bb_dfs(st, 0, 0, 0);
    }

    out.profit = st.best_profit;
    out.weight = st.best_weight;
    out.nodes = st.nodes;
    out.hit_limit = st.hit_limit;
    out.closed_gap = st.closed_gap || st.best_profit >= global_upper;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].id >= 0 && static_cast<std::size_t>(items[i].id) < out.multiplicity_by_id.size()) {
            out.multiplicity_by_id[static_cast<std::size_t>(items[i].id)] += st.best[i];
        }
    }
    return out;
}

struct AdaptiveBBController {
    long long probe_nodes = 0;
    long long max_nodes = 0;
    bool enabled = true;

    explicit AdaptiveBBController(long long configured_limit) {
        max_nodes = std::max<long long>(0, configured_limit);
        // The probe is intentionally small.  It is large enough to catch easy
        // core-dominated instances, but small enough not to penalize adverse ones.
        probe_nodes = std::min<long long>(max_nodes, 128);
    }

    bool should_escalate(const CoreBBResult& probe,
                         Profit old_incumbent,
                         Profit global_upper,
                         long long items_after_preprocess) const {
        if (!enabled || max_nodes <= probe_nodes) return false;
        if (probe.closed_gap) return false;

        const bool improved = probe.profit > old_incumbent;
        const Profit gap = std::max<Profit>(0, global_upper - probe.profit);
        const long double relative_gap = global_upper > 0
            ? static_cast<long double>(gap) / static_cast<long double>(global_upper)
            : 1.0L;

        // Escalate only if the B&B gave evidence that it is promising: either it
        // improves the incumbent and leaves a very small gap, or preprocessing has
        // already reduced the instance to a small effective core.
        if (improved && relative_gap <= 0.0005L) return true;
        if (items_after_preprocess <= 256 && relative_gap <= 0.002L) return true;
        return false;
    }
};

#endif

using detail::AdaptiveBBController;
using detail::CoreBBResult;
using detail::run_core_branch_and_bound;

struct BoundSampler {
    const BoundContext& ctx;
    Weight capacity = 0;
    Profit incumbent = 0;
    long long calls = 0;
    long long fathomed = 0;
    long long checks = 0;
    long long states_seen = 0;
    bool active = true;
    long long interval = 32768;
    long long window_calls = 0;
    long long window_fathomed = 0;
    long long max_calls = 8192;

    BoundSampler(const BoundContext& c, Weight cap, Profit inc, long long initial_interval, long long call_budget)
        : ctx(c), capacity(cap), incumbent(inc),
          interval(std::max<long long>(1, initial_interval)),
          max_calls(std::max<long long>(0, call_budget)) {}

    bool should_check() const {
        return active && calls < max_calls && interval > 0 && (states_seen % interval == 0);
    }

    void observe(Weight y, Profit prefix_profit) {
        ++states_seen;
        if (!should_check()) return;

        BoundValue b = compute_bound(ctx, capacity - y);
        ++calls;
        ++checks;
        ++window_calls;
        if (safe_add(prefix_profit, b.upper) <= incumbent) {
            ++fathomed;
            ++window_fathomed;
        }

        // Bound sampling policy.  It reacts to the useful cuts per sampled bound,
        // and intentionally becomes conservative quickly.  Correctness is not at
        // risk because the dense DP remains exact even if sampling is disabled.
        if (calls >= max_calls) {
            active = false;
            return;
        }

        if (window_calls >= 128) {
            const long double cut_ratio = static_cast<long double>(window_fathomed) /
                                          static_cast<long double>(window_calls);
            if (cut_ratio < 0.01L) {
                active = false;
            } else if (cut_ratio < 0.10L) {
                interval *= 8;
            } else if (cut_ratio < 0.35L) {
                interval *= 2;
            }
            // Do not make the interval denser in the DP fallback.  A favorable
            // instance should normally have been solved by core B&B already;
            // after that point bounds are just a sampled guardrail.
            window_calls = 0;
            window_fathomed = 0;
        }
    }
};



struct ExactReducedCoreResult {
    bool solved = false;
    Solution solution;
    long long states_scanned = 0;
};

// Exact full-capacity solver for structurally reduced cores.
//
// This is not selected by a fixed item-count rule.  The caller uses a dynamic
// work estimate.  When the reduced core makes full DP cheaper than the
// speculative sequence/half-split machinery, this path mimics PYAsUKP's short
// exit behavior: solve the small remaining core directly and return.
ExactReducedCoreResult solve_reduced_core_exact(const Instance& inst,
                                                std::vector<Item> items,
                                                long long transition_budget) {
    ExactReducedCoreResult out;
    if (items.empty() || inst.capacity < 0) return out;

    std::sort(items.begin(), items.end(),
        [](const Item& a, const Item& b) {
            if (a.w != b.w) return a.w < b.w;
            return better_ratio(a, b);
        });

    const size_t cap = static_cast<size_t>(inst.capacity);
    std::vector<Profit> dp(cap + 1, 0);
    std::vector<int> last(cap + 1, -1);
    std::vector<Weight> prev(cap + 1, 0);

    std::vector<Weight> w;
    std::vector<Profit> p;
    std::vector<int> id;
    w.reserve(items.size());
    p.reserve(items.size());
    id.reserve(items.size());
    for (const Item& it : items) {
        if (it.w <= 0 || it.w > inst.capacity) continue;
        w.push_back(it.w);
        p.push_back(it.p);
        id.push_back(it.id);
    }

    std::size_t usable = 0;
    for (Weight y = 1; y <= inst.capacity; ++y) {
        while (usable < w.size() && w[usable] <= y) ++usable;

        Profit best_profit = dp[static_cast<size_t>(y)];
        int best_last = last[static_cast<size_t>(y)];
        Weight best_prev = prev[static_cast<size_t>(y)];

        for (std::size_t i = 0; i < usable; ++i) {
            ++out.states_scanned;
            if (out.states_scanned > transition_budget) return out;

            const Weight wi = w[i];
            const Profit cand = safe_add(dp[static_cast<size_t>(y - wi)], p[i]);
            if (cand > best_profit) {
                best_profit = cand;
                best_last = id[i];
                best_prev = y - wi;
            }
        }

        dp[static_cast<size_t>(y)] = best_profit;
        last[static_cast<size_t>(y)] = best_last;
        prev[static_cast<size_t>(y)] = best_prev;
    }

    Weight best_w = 0;
    Profit best_p = 0;
    for (Weight y = 0; y <= inst.capacity; ++y) {
        if (dp[static_cast<size_t>(y)] > best_p ||
            (dp[static_cast<size_t>(y)] == best_p && y > best_w)) {
            best_p = dp[static_cast<size_t>(y)];
            best_w = y;
        }
    }

    Solution sol;
    sol.profit = best_p;
    sol.weight = 0;
    sol.optimal = true;
    sol.solver_name = "optimized";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);

    Weight y = best_w;
    while (y > 0) {
        const int item_id = last[static_cast<size_t>(y)];
        if (item_id < 0) break;

        const auto it = std::find_if(inst.items.begin(), inst.items.end(),
            [&](const Item& x) { return x.id == item_id; });
        if (it == inst.items.end()) throw std::runtime_error("reduced-core backtracking failed");

        sol.multiplicity_by_id[static_cast<std::size_t>(item_id)]++;
        sol.weight += it->w;
        y = prev[static_cast<size_t>(y)];
    }

    out.solved = true;
    out.solution = std::move(sol);
    return out;
}

// Safe collective dominance using only previously kept, better/equal-ratio items.
//
// This is a conservative C++ analogue of PYAsUKP's collective dominance stage:
// item j is removed only when an explicit unbounded DP certificate from earlier
// kept items achieves profit >= p_j within weight <= w_j.
std::vector<Item> remove_collectively_dominated_safe(std::vector<Item> items, Weight capacity) {
    if (items.size() <= 1 || capacity <= 0) return items;

    std::sort(items.begin(), items.end(), better_ratio);

    Weight max_w = 0;
    for (const Item& it : items) {
        if (it.w > 0 && it.w <= capacity) max_w = std::max(max_w, it.w);
    }
    if (max_w <= 0) return items;

    std::vector<Profit> best(static_cast<size_t>(max_w) + 1, 0);
    std::vector<Item> kept;
    kept.reserve(items.size());

    for (const Item& it : items) {
        if (it.w <= 0 || it.w > capacity) continue;

        if (best[static_cast<size_t>(it.w)] >= it.p) {
            continue;
        }

        kept.push_back(it);
        for (Weight y = it.w; y <= max_w; ++y) {
            const Profit cand = safe_add(best[static_cast<size_t>(y - it.w)], it.p);
            if (cand > best[static_cast<size_t>(y)]) {
                best[static_cast<size_t>(y)] = cand;
            }
        }
    }

    return kept.empty() ? items : kept;
}

// Immediate exact stop for the case where the dynamic reductions leave one item.
// This is the C++ counterpart of PYAsUKP reporting "Remaining undominated items: 1"
// and finishing without running the full sequence machinery.
bool try_single_remaining_item_stop(const Instance& inst,
                                    const std::vector<Item>& items,
                                    Solution& sol) {
    if (items.size() != 1) return false;

    const Item& it = items.front();
    if (it.w <= 0) return false;

    const long long x = inst.capacity / it.w;
    sol.profit = safe_mul(x, it.p);
    sol.weight = safe_mul(x, it.w);
    sol.optimal = true;
    sol.solver_name = "optimized";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);
    if (it.id >= 0 && static_cast<std::size_t>(it.id) < sol.multiplicity_by_id.size()) {
        sol.multiplicity_by_id[static_cast<std::size_t>(it.id)] = x;
    }
    return true;
}


struct ExactCoreClosureResult {
    bool closed = false;
    Solution solution;
    long long nodes = 0;
};

// Exact OCaml-like bound stop for a reduced core.
//
// PYAsUKP often stops before forward DP when the bound/core machinery proves the
// value.  This routine gives the C++ implementation the same opportunity: after
// preprocessing and dominance reductions, try to solve the remaining core with a
// depth-first bounded enumeration ordered by profit/weight.  It is exact when it
// returns closed=true; otherwise it is ignored and the normal exact DP path runs.
//
// The decision to call this routine is made by work estimates in Solver::solve,
// not by a fixed item-count threshold.
ExactCoreClosureResult try_exact_core_bound_stop(const Instance& inst,
                                                 std::vector<Item> core,
                                                 Profit global_upper,
                                                 Profit incumbent,
                                                 const std::vector<long long>& incumbent_mult,
                                                 long long node_budget) {
    ExactCoreClosureResult out;
    if (core.empty() || node_budget <= 0 || inst.capacity < 0) return out;

    std::sort(core.begin(), core.end(), better_ratio);

    Solution best_sol;
    best_sol.profit = incumbent;
    best_sol.weight = 0;
    best_sol.optimal = true;
    best_sol.solver_name = "optimized";
    best_sol.multiplicity_by_id = incumbent_mult;
    if (best_sol.multiplicity_by_id.size() != inst.items.size()) {
        best_sol.multiplicity_by_id.assign(inst.items.size(), 0);
    }
    for (const Item& it : inst.items) {
        if (it.id >= 0 && static_cast<std::size_t>(it.id) < best_sol.multiplicity_by_id.size()) {
            best_sol.weight += safe_mul(best_sol.multiplicity_by_id[static_cast<std::size_t>(it.id)], it.w);
        }
    }

    // Greedy incumbent.  Accept equal profit if it gives a concrete/non-empty
    // certificate.  This avoids the invalid "profit with zero weight" failure.
    if (!core.empty() && core.front().w > 0) {
        const long long x = inst.capacity / core.front().w;
        const Profit gp = safe_mul(x, core.front().p);
        const Weight gw = safe_mul(x, core.front().w);
        if (gp > best_sol.profit || (gp == best_sol.profit && gw > best_sol.weight)) {
            best_sol.profit = gp;
            best_sol.weight = gw;
            best_sol.multiplicity_by_id.assign(inst.items.size(), 0);
            if (core.front().id >= 0 &&
                static_cast<std::size_t>(core.front().id) < best_sol.multiplicity_by_id.size()) {
                best_sol.multiplicity_by_id[static_cast<std::size_t>(core.front().id)] = x;
            }
        }
    }

    std::vector<long long> cur(inst.items.size(), 0);
    std::vector<long long> best_mult = best_sol.multiplicity_by_id;
    Profit best_profit = best_sol.profit;
    Weight best_weight = best_sol.weight;
    bool exhausted = true;

    auto suffix_fractional_bound = [&](std::size_t k, Weight rem) -> Profit {
        if (rem <= 0 || k >= core.size()) return 0;
        const Item& it = core[k];
        return floor_mul_div(rem, it.p, it.w);
    };

    std::function<void(std::size_t, Weight, Profit)> dfs =
        [&](std::size_t k, Weight used_w, Profit used_p) {
            if (out.nodes >= node_budget) {
                exhausted = false;
                return;
            }
            ++out.nodes;

            if (used_p > best_profit || (used_p == best_profit && used_w > best_weight)) {
                best_profit = used_p;
                best_weight = used_w;
                best_mult = cur;
                if (best_profit >= global_upper) {
                    return;
                }
            }

            if (k >= core.size()) return;

            const Weight rem = inst.capacity - used_w;
            if (rem <= 0) return;

            // Fractional upper bound from the current best remaining item.  If
            // this cannot beat the incumbent, the whole subtree is closed.
            const Profit optimistic = safe_add(used_p, suffix_fractional_bound(k, rem));
            if (optimistic <= best_profit) return;

            const Item& it = core[k];
            if (it.w <= 0) return;

            const long long max_x = rem / it.w;

            // High multiplicities first, as in the existing core B&B and in the
            // spirit of PYAsUKP's bound-first behavior.
            for (long long x = max_x; x >= 0; --x) {
                if (best_profit >= global_upper) return;
                if (out.nodes >= node_budget) {
                    exhausted = false;
                    return;
                }

                const Weight nw = used_w + safe_mul(x, it.w);
                const Profit np = safe_add(used_p, safe_mul(x, it.p));
                const Weight nrem = inst.capacity - nw;

                Profit child_bound = np;
                if (k + 1 < core.size()) {
                    child_bound = safe_add(child_bound, suffix_fractional_bound(k + 1, nrem));
                }
                if (child_bound <= best_profit) {
                    if (x == 0) break;
                    continue;
                }

                if (it.id >= 0 && static_cast<std::size_t>(it.id) < cur.size()) {
                    cur[static_cast<std::size_t>(it.id)] = x;
                }

                if (np > best_profit || (np == best_profit && nw > best_weight)) {
                    best_profit = np;
                    best_weight = nw;
                    best_mult = cur;
                    if (best_profit >= global_upper) {
                        if (it.id >= 0 && static_cast<std::size_t>(it.id) < cur.size()) {
                            cur[static_cast<std::size_t>(it.id)] = 0;
                        }
                        return;
                    }
                }

                if (k + 1 < core.size()) {
                    dfs(k + 1, nw, np);
                }

                if (it.id >= 0 && static_cast<std::size_t>(it.id) < cur.size()) {
                    cur[static_cast<std::size_t>(it.id)] = 0;
                }

                if (x == 0) break;
            }
        };

    dfs(0, 0, 0);

    // Closed if either the theoretical upper bound was reached, or the complete
    // bounded enumeration exhausted all remaining candidates within budget.
    if (best_profit >= global_upper || exhausted) {
        Solution sol;
        sol.profit = best_profit;
        sol.weight = 0;
        sol.optimal = true;
        sol.solver_name = "optimized";
        sol.multiplicity_by_id = std::move(best_mult);
        for (const Item& it : inst.items) {
            if (it.id >= 0 && static_cast<std::size_t>(it.id) < sol.multiplicity_by_id.size()) {
                sol.weight += safe_mul(sol.multiplicity_by_id[static_cast<std::size_t>(it.id)], it.w);
            }
        }
        out.closed = true;
        out.solution = std::move(sol);
    }

    return out;
}

struct SparseSequenceEntry {
    Profit profit = 0;
    Weight prev_weight = 0;
    int last_item = -1;
};

struct SparseSequenceResult {
    bool solved = false;
    Solution solution;
    long long states_scanned = 0;
    long long critical_points = 0;
};

// Experimental sequence_result-style backend.  It stores only capacities that
// are actually reached by generated transitions instead of allocating a full
// table first.  The method is exact when it runs to completion.  If the number
// of points or transitions grows beyond the configured budget, the caller falls
// back to the dense exact DP below.
//
// This is not a line-by-line port of PYAsUKP's Seq/Slice modules yet, but it is
// the first safe replacement of the dense table by an explicit sequence of
// critical/reachable states in this C++ implementation.
SparseSequenceResult try_sparse_sequence_dp(const Instance& inst,
                                            const std::vector<Item>& items,
                                            std::size_t max_points,
                                            long long max_transitions) {
    SparseSequenceResult out;
    if (items.empty()) return out;

    std::unordered_map<Weight, SparseSequenceEntry> best;
    best.reserve(std::min<std::size_t>(max_points, 1u << 20));

    std::deque<Weight> queue;
    std::unordered_set<Weight> in_queue;
    in_queue.reserve(std::min<std::size_t>(max_points, 1u << 20));

    best.emplace(0, SparseSequenceEntry{0, 0, -1});
    queue.push_back(0);
    in_queue.insert(0);

    Profit best_profit = 0;
    Weight best_weight = 0;

    while (!queue.empty()) {
        Weight y = queue.front();
        queue.pop_front();
        in_queue.erase(y);

        const Profit base_profit = best.find(y)->second.profit;

        for (const Item& it : items) {
            const Weight ny = y + it.w;
            if (ny > inst.capacity) continue;

            ++out.states_scanned;
            if (out.states_scanned > max_transitions) {
                return out;
            }

            const Profit np = safe_add(base_profit, it.p);
            auto pos = best.find(ny);
            if (pos == best.end() || np > pos->second.profit) {
                best[ny] = SparseSequenceEntry{np, y, it.id};

                if (np > best_profit || (np == best_profit && ny > best_weight)) {
                    best_profit = np;
                    best_weight = ny;
                }

                if (best.size() > max_points) {
                    return out;
                }

                if (in_queue.insert(ny).second) {
                    queue.push_back(ny);
                }
            }
        }
    }

    Solution sol;
    sol.profit = best_profit;
    sol.weight = 0;
    sol.optimal = true;
    sol.solver_name = "optimized";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);

    Weight y = best_weight;
    while (y > 0) {
        auto pos = best.find(y);
        if (pos == best.end() || pos->second.last_item < 0) {
            out.solved = false;
            return out;
        }

        const int id = pos->second.last_item;
        const auto it = std::find_if(inst.items.begin(), inst.items.end(),
            [&](const Item& x) { return x.id == id; });
        if (it == inst.items.end()) {
            out.solved = false;
            return out;
        }

        sol.multiplicity_by_id[static_cast<std::size_t>(id)]++;
        sol.weight += it->w;
        y = pos->second.prev_weight;
    }

    out.solved = true;
    out.solution = std::move(sol);
    out.critical_points = static_cast<long long>(best.size());
    return out;
}


struct SequenceDrivenHalfResult {
    bool solved = false;
    Solution solution;
    long long states_scanned = 0;
    long long critical_points = 0;
};

// Exact sequence_result-driven half-split solver.
//
// The dense half-split DP scans capacities and items.  This routine is instead
// driven by a queue of exact-weight states whose value improved.  It computes
// exact-weight closure up to prefix_limit, converts it to capacity values by a
// prefix maximum, and composes two prefix solutions.  If the reachable graph
// becomes dense, it returns solved=false and the caller uses the existing exact
// half-split DP.
SequenceDrivenHalfResult try_sequence_driven_half_split(
    const Instance& inst,
    const std::vector<Item>& items,
    Weight prefix_limit,
    long long max_transitions,
    std::size_t max_critical_points) {

    SequenceDrivenHalfResult out;
    if (items.empty() || prefix_limit < 0) return out;

    const size_t limit = static_cast<size_t>(prefix_limit);
    const Profit neg_inf = std::numeric_limits<Profit>::min() / 4;

    std::vector<Profit> exact_profit(limit + 1, neg_inf);
    std::vector<int> exact_last(limit + 1, -1);
    std::vector<Weight> exact_prev(limit + 1, 0);
    std::vector<unsigned char> in_queue(limit + 1, 0);
    std::deque<Weight> queue;

    exact_profit[0] = 0;
    queue.push_back(0);
    in_queue[0] = 1;

    std::vector<Weight> sequence_result;
    sequence_result.reserve(std::min<std::size_t>(max_critical_points, 1u << 20));
    sequence_result.push_back(0);

    while (!queue.empty()) {
        const Weight y = queue.front();
        queue.pop_front();
        in_queue[static_cast<size_t>(y)] = 0;

        const Profit base = exact_profit[static_cast<size_t>(y)];
        if (base == neg_inf) continue;

        for (const Item& it : items) {
            const Weight ny = y + it.w;
            if (ny > prefix_limit) continue;

            ++out.states_scanned;
            if (out.states_scanned > max_transitions) return out;

            const Profit np = safe_add(base, it.p);
            const size_t ni = static_cast<size_t>(ny);
            if (np > exact_profit[ni]) {
                if (exact_profit[ni] == neg_inf) {
                    sequence_result.push_back(ny);
                    if (sequence_result.size() > max_critical_points) return out;
                }

                exact_profit[ni] = np;
                exact_last[ni] = it.id;
                exact_prev[ni] = y;

                if (!in_queue[ni]) {
                    queue.push_back(ny);
                    in_queue[ni] = 1;
                }
            }
        }
    }

    std::vector<Profit> best_leq_profit(limit + 1, 0);
    std::vector<Weight> best_leq_weight(limit + 1, 0);

    Profit running_best = 0;
    Weight running_weight = 0;
    for (Weight y = 0; y <= prefix_limit; ++y) {
        const Profit v = exact_profit[static_cast<size_t>(y)];
        if (v > running_best) {
            running_best = v;
            running_weight = y;
        }
        best_leq_profit[static_cast<size_t>(y)] = running_best;
        best_leq_weight[static_cast<size_t>(y)] = running_weight;
    }

    Profit best_profit = 0;
    Weight best_a_weight = 0;
    Weight best_b_weight = 0;

    for (Weight a = 0; a <= prefix_limit; ++a) {
        const Weight remaining = inst.capacity - a;
        const Weight bcap = std::min<Weight>(prefix_limit, std::max<Weight>(0, remaining));
        const Profit val = safe_add(best_leq_profit[static_cast<size_t>(a)],
                                    best_leq_profit[static_cast<size_t>(bcap)]);
        if (val > best_profit) {
            best_profit = val;
            best_a_weight = best_leq_weight[static_cast<size_t>(a)];
            best_b_weight = best_leq_weight[static_cast<size_t>(bcap)];
        }
    }

    Solution sol;
    sol.profit = best_profit;
    sol.weight = 0;
    sol.optimal = true;
    sol.solver_name = "optimized";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);

    auto add_exact_trace = [&](Weight start_w) {
        Weight y = start_w;
        while (y > 0) {
            const size_t yi = static_cast<size_t>(y);
            const int id = exact_last[yi];
            if (id < 0) throw std::runtime_error("sequence-driven backtracking failed");

            const auto it = std::find_if(inst.items.begin(), inst.items.end(),
                [&](const Item& x) { return x.id == id; });
            if (it == inst.items.end()) throw std::runtime_error("sequence-driven item id not found");

            sol.multiplicity_by_id[static_cast<std::size_t>(id)]++;
            sol.weight += it->w;
            y = exact_prev[yi];
        }
    };

    add_exact_trace(best_a_weight);
    add_exact_trace(best_b_weight);

    out.solved = true;
    out.solution = std::move(sol);
    out.critical_points = static_cast<long long>(sequence_result.size());
    return out;
}


struct SliceMergeHalfResult {
    bool solved = false;
    Solution solution;
    long long states_scanned = 0;
    long long critical_points = 0;
};

struct SliceCandidate {
    Weight w = 0;
    Profit p = 0;
    Weight prev_w = 0;
    int last_item = -1;

    bool operator>(const SliceCandidate& other) const {
        if (w != other.w) return w > other.w;
        return p < other.p;
    }
};

// A closer C++ analogue of PYAsUKP's Slice.one + merge/filter idea.
//
// The solver is driven by the current sequence_result rather than by scanning
// every capacity for every item.  For each slice [lo, hi], it generates only
// transitions from current critical points into that slice, merges candidates
// by target weight, and filters dominated points.  It is exact if it completes.
// If the number of generated transitions or critical points exceeds the budget,
// it returns solved=false and the caller keeps the existing exact half-split DP.
SliceMergeHalfResult try_slice_one_merge_half_split(
    const Instance& inst,
    const std::vector<Item>& input_items,
    Weight prefix_limit,
    Weight slice_height,
    long long max_transitions,
    std::size_t max_critical_points) {

    SliceMergeHalfResult out;
    if (input_items.empty() || prefix_limit < 0) return out;

    std::vector<Item> items = input_items;
    std::sort(items.begin(), items.end(),
        [](const Item& a, const Item& b) {
            if (a.w != b.w) return a.w < b.w;
            return better_ratio(a, b);
        });

    struct SeqPoint {
        Weight w = 0;
        Profit p = 0;
        Weight prev_w = 0;
        int last_item = -1;
    };

    const size_t limit = static_cast<size_t>(prefix_limit);
    const Profit neg_inf = std::numeric_limits<Profit>::min() / 4;

    // exact_profit is a membership/profit oracle, not the driver.  The driver is
    // sequence_result.  Keeping this array avoids unordered_map overhead and
    // keeps correctness checks O(1).
    std::vector<Profit> exact_profit(limit + 1, neg_inf);
    std::vector<int> exact_last(limit + 1, -1);
    std::vector<Weight> exact_prev(limit + 1, 0);

    std::vector<SeqPoint> sequence_result;
    sequence_result.reserve(std::min<std::size_t>(max_critical_points, 1u << 20));
    sequence_result.push_back(SeqPoint{0, 0, 0, -1});
    exact_profit[0] = 0;

    Weight lo = 1;
    while (lo <= prefix_limit) {
        const Weight hi = std::min<Weight>(prefix_limit, lo + slice_height - 1);

        // Active item filtering by slice.
        //
        // This is a conservative filter: an item is considered in this slice only
        // if it can land at least one transition inside [lo, hi] from some current
        // critical point.  It does not remove the item globally and therefore does
        // not affect the fallback exact DP.  The goal is to avoid generating the
        // full item set for slices where many items cannot contribute.
        std::vector<const Item*> slice_items;
        slice_items.reserve(items.size());

        for (const Item& it : items) {
            if (it.w > hi) break;

            bool reaches_slice = false;
            for (const SeqPoint& sp : sequence_result) {
                if (sp.w >= hi) break;
                if (sp.w + it.w > hi) continue;

                const Weight gap = lo > sp.w ? lo - sp.w : 0;
                const long long kmin = gap <= 0 ? 1 : std::max<long long>(1, (gap + it.w - 1) / it.w);
                const Weight first = sp.w + safe_mul(kmin, it.w);
                if (first >= lo && first <= hi) {
                    reaches_slice = true;
                    break;
                }
            }

            if (reaches_slice) {
                slice_items.push_back(&it);
            }
        }

        if (slice_items.empty()) {
            lo = hi + 1;
            continue;
        }

        // Priority-queue merge of generated candidates, similar in spirit to a
        // k-way merge of shifted sequences.  The queue stores candidate states
        // landing in the current slice.
        std::priority_queue<SliceCandidate,
                            std::vector<SliceCandidate>,
                            std::greater<SliceCandidate>> pq;

        for (const SeqPoint& sp : sequence_result) {
            if (sp.p == neg_inf) continue;

            for (const Item* pit : slice_items) {
                const Item& it = *pit;
                Weight nw = sp.w + it.w;
                if (nw > hi) {
                    continue;
                }

                // Generate multiples of this item from this sequence point only
                // inside the current slice.  This is the key difference from the
                // dense loop: capacities that are not induced by a critical point
                // are not used as drivers.
                Profit np = safe_add(sp.p, it.p);
                while (nw <= hi) {
                    if (nw >= lo) {
                        pq.push(SliceCandidate{nw, np, sp.w, it.id});
                    }

                    ++out.states_scanned;
                    if (out.states_scanned > max_transitions) return out;

                    nw += it.w;
                    np = safe_add(np, it.p);
                }
            }
        }

        // Merge candidates with same target weight, keeping only best profit.
        std::vector<SeqPoint> new_points;
        Profit best_profit_seen = sequence_result.empty() ? 0 : sequence_result.back().p;

        while (!pq.empty()) {
            SliceCandidate cur = pq.top();
            pq.pop();

            Weight w = cur.w;
            Profit best_p = cur.p;
            Weight best_prev = cur.prev_w;
            int best_last = cur.last_item;

            while (!pq.empty() && pq.top().w == w) {
                SliceCandidate other = pq.top();
                pq.pop();
                if (other.p > best_p) {
                    best_p = other.p;
                    best_prev = other.prev_w;
                    best_last = other.last_item;
                }
            }

            const size_t wi = static_cast<size_t>(w);
            if (best_p > exact_profit[wi]) {
                exact_profit[wi] = best_p;
                exact_prev[wi] = best_prev;
                exact_last[wi] = best_last;

                // Filter: append only critical points, i.e., points that improve
                // the upper envelope f(y)=max_{x<=y} profit[x].
                if (best_p > best_profit_seen) {
                    new_points.push_back(SeqPoint{w, best_p, best_prev, best_last});
                    best_profit_seen = best_p;
                }
            }
        }

        if (!new_points.empty()) {
            // Merge/filter with previous sequence_result.  Since new_points are
            // in increasing weight order and strictly improve the envelope, this
            // is append-only for the current slice.
            for (const SeqPoint& p : new_points) {
                sequence_result.push_back(p);
                if (sequence_result.size() > max_critical_points) return out;
            }
        }

        // If no new critical point appeared in the slice, continue.  This is not
        // a proof of periodicity by itself, but it keeps the solver exact.
        lo = hi + 1;
    }

    if (sequence_result.empty()) return out;

    // Convert the critical sequence to capacity values by walking the envelope.
    std::vector<Profit> best_leq_profit(limit + 1, 0);
    std::vector<Weight> best_leq_weight(limit + 1, 0);

    std::size_t k = 0;
    Profit current_p = 0;
    Weight current_w = 0;
    for (Weight y = 0; y <= prefix_limit; ++y) {
        while (k < sequence_result.size() && sequence_result[k].w <= y) {
            if (sequence_result[k].p > current_p) {
                current_p = sequence_result[k].p;
                current_w = sequence_result[k].w;
            }
            ++k;
        }
        best_leq_profit[static_cast<size_t>(y)] = current_p;
        best_leq_weight[static_cast<size_t>(y)] = current_w;
    }

    Profit best_profit = 0;
    Weight best_a_weight = 0;
    Weight best_b_weight = 0;

    for (Weight a = 0; a <= prefix_limit; ++a) {
        const Weight remaining = inst.capacity - a;
        const Weight bcap = std::min<Weight>(prefix_limit, std::max<Weight>(0, remaining));
        const Profit val = safe_add(best_leq_profit[static_cast<size_t>(a)],
                                    best_leq_profit[static_cast<size_t>(bcap)]);
        if (val > best_profit) {
            best_profit = val;
            best_a_weight = best_leq_weight[static_cast<size_t>(a)];
            best_b_weight = best_leq_weight[static_cast<size_t>(bcap)];
        }
    }

    Solution sol;
    sol.profit = best_profit;
    sol.weight = 0;
    sol.optimal = true;
    sol.solver_name = "optimized";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);

    auto add_trace = [&](Weight start_w) {
        Weight y = start_w;
        while (y > 0) {
            const size_t yi = static_cast<size_t>(y);
            const int id = exact_last[yi];
            if (id < 0) throw std::runtime_error("slice-merge backtracking failed");

            const auto it = std::find_if(inst.items.begin(), inst.items.end(),
                [&](const Item& x) { return x.id == id; });
            if (it == inst.items.end()) throw std::runtime_error("slice-merge item id not found");

            sol.multiplicity_by_id[static_cast<std::size_t>(id)]++;
            sol.weight += it->w;
            y = exact_prev[yi];
        }
    };

    add_trace(best_a_weight);
    add_trace(best_b_weight);

    out.solved = true;
    out.solution = std::move(sol);
    out.critical_points = static_cast<long long>(sequence_result.size());
    return out;
}

}  // namespace

Solver::Solver(SolverOptions options) : options_(options) {}

SolverResult Solver::solve(const Instance& inst) {
    if (inst.capacity < 0) throw std::invalid_argument("negative capacity");

    SolverResult result;
    result.stats.original_items = static_cast<long long>(inst.items.size());
    if (inst.items.empty() || inst.capacity == 0) {
        result.solution.multiplicity_by_id.assign(inst.items.size(), 0);
        result.solution.optimal = true;
        result.solution.solver_name = "optimized";
        return result;
    }

    std::vector<Item> items = inst.items;
    if (options_.use_preprocessing) {
        items = remove_simple_dominated(items);
        Item best = *std::max_element(items.begin(), items.end(),
            [](const Item& a, const Item& b) { return better_ratio(b, a); });
        items = remove_multiple_dominated_by_best(items, best);

        // Collective dominance, with explicit replacement certificate.  This is
        // safe and placed before all path-selection decisions so the controller
        // sees the true reduced core.
        items = remove_collectively_dominated_safe(std::move(items), inst.capacity);
    }
    std::sort(items.begin(), items.end(), better_ratio);
    result.stats.after_preprocess_items = static_cast<long long>(items.size());

    {
        Solution one_item_solution;
        if (try_single_remaining_item_stop(inst, items, one_item_solution)) {
            result.solution = std::move(one_item_solution);
            return result;
        }
    }

    BoundContext ctx = make_bound_context(items);
    BoundValue global_bound = compute_bound(ctx, inst.capacity);
    result.stats.bound_calls++;

    Profit incumbent = safe_mul(inst.capacity / ctx.best.w, ctx.best.p);
    std::vector<long long> incumbent_mult(inst.items.size(), 0);
    if (ctx.best.id >= 0 && static_cast<std::size_t>(ctx.best.id) < incumbent_mult.size()) {
        incumbent_mult[static_cast<std::size_t>(ctx.best.id)] = inst.capacity / ctx.best.w;
    }

    // Bound-stop faithful to the PYAsUKP behavior: if the initial incumbent
    // already closes the upper bound, do not enter sequence/DP machinery.
    if (options_.use_bounds && incumbent >= global_bound.upper) {
        Solution sol;
        sol.profit = incumbent;
        sol.weight = 0;
        sol.multiplicity_by_id = incumbent_mult;
        sol.optimal = true;
        sol.solver_name = "optimized";
        for (const Item& it : inst.items) {
            if (it.id >= 0 && static_cast<std::size_t>(it.id) < sol.multiplicity_by_id.size()) {
                sol.weight += it.w * sol.multiplicity_by_id[static_cast<std::size_t>(it.id)];
            }
        }
        result.solution = std::move(sol);
        return result;
    }

    const long double preprocess_ratio = result.stats.original_items > 0
        ? static_cast<long double>(result.stats.after_preprocess_items) /
              static_cast<long double>(result.stats.original_items)
        : 1.0L;

    // Structural diagnosis used by the adaptive policy.  When preprocessing
    // removes almost nothing, the original EDUK2 hybrid machinery tends to pay
    // for many bound/B&B operations without reducing the DP space enough.  In
    // that regime we keep only a very small B&B probe and fall back to the dense
    // cache-friendly DP.
    const bool structurally_bb_friendly =
        result.stats.after_preprocess_items <= 256 || preprocess_ratio <= 0.35L;

    // If preprocessing did not reduce the instance, the EDUK2 hybrid machinery
    // is usually expensive: B&B explores its full budget and contextual bounds
    // cut too few states.  In this regime the optimized solver deliberately
    // skips the hybrid phase and falls back to the exact cache-friendly DP.
    //
    // Keep this variable name stable: it is useful to verify that this file is
    // really the one being compiled:
    //   grep -n "skip_hybrid" src/optimized/optimized_solver.cpp
    const bool structurally_adverse =
        result.stats.after_preprocess_items > 512 && preprocess_ratio >= 0.80L;
    const bool skip_hybrid = structurally_adverse;

    bool enable_dp_bound_sampling = structurally_bb_friendly && !skip_hybrid;

    // Important: if the structural diagnosis says the instance is adverse,
    // the optimized solver does not even run the B&B probe.  Earlier versions
    // still spent the full B&B budget in this regime; that is exactly the
    // overhead this policy is meant to avoid.  Correctness is preserved because
    // the subsequent dynamic program is exact.

    // Hybrid/adaptive B&B.  First run a small probe.  Escalate only when the
    // probe gives evidence that the instance is B&B-friendly.
    if (options_.use_core_bb && options_.bb_node_limit > 0 && !skip_hybrid) {
        AdaptiveBBController controller(options_.bb_node_limit);
        if (!structurally_bb_friendly) {
            controller.max_nodes = std::min<long long>(controller.max_nodes, 512);
            controller.probe_nodes = std::min<long long>(controller.max_nodes, 64);
        }
        const Profit before_probe = incumbent;
        CoreBBResult probe = run_core_branch_and_bound(items, inst.capacity, controller.probe_nodes,
                                         options_.core_size, incumbent,
                                         global_bound.upper, inst.items.size());
        result.stats.bb_nodes += probe.nodes;
        if (probe.profit > incumbent) {
            incumbent = probe.profit;
            incumbent_mult = probe.multiplicity_by_id;
        }
        {
            const Profit gap = std::max<Profit>(0, global_bound.upper - incumbent);
            const long double relative_gap = global_bound.upper > 0
                ? static_cast<long double>(gap) / static_cast<long double>(global_bound.upper)
                : 1.0L;
            if (probe.profit > before_probe && relative_gap <= 0.002L) {
                enable_dp_bound_sampling = true;
            }
        }
        if (options_.use_bounds && (probe.closed_gap || incumbent >= global_bound.upper)) {
            Solution sol;
            sol.profit = incumbent;
            sol.weight = 0;
            sol.multiplicity_by_id = incumbent_mult;
            sol.optimal = true;
            sol.solver_name = "optimized";
            for (const Item& it : inst.items) {
                if (it.id >= 0 && static_cast<std::size_t>(it.id) < sol.multiplicity_by_id.size()) {
                    sol.weight += it.w * sol.multiplicity_by_id[static_cast<std::size_t>(it.id)];
                }
            }
            result.solution = std::move(sol);
            return result;
        }

        if (controller.should_escalate(probe, before_probe, global_bound.upper,
                                       result.stats.after_preprocess_items)) {
            const long long remaining_nodes = controller.max_nodes - result.stats.bb_nodes;
            if (remaining_nodes > 0) {
                CoreBBResult extended = run_core_branch_and_bound(items, inst.capacity, remaining_nodes,
                                                    options_.core_size, incumbent,
                                                    global_bound.upper, inst.items.size());
                result.stats.bb_nodes += extended.nodes;
                if (extended.profit > incumbent) {
                    incumbent = extended.profit;
                    incumbent_mult = extended.multiplicity_by_id;
                    enable_dp_bound_sampling = true;
                }
                if (options_.use_bounds && (extended.closed_gap || incumbent >= global_bound.upper)) {
                    Solution sol;
                    sol.profit = incumbent;
                    sol.weight = 0;
                    sol.multiplicity_by_id = incumbent_mult;
                    sol.optimal = true;
                    sol.solver_name = "optimized";
                    for (const Item& it : inst.items) {
                        if (it.id >= 0 && static_cast<std::size_t>(it.id) < sol.multiplicity_by_id.size()) {
                            sol.weight += it.w * sol.multiplicity_by_id[static_cast<std::size_t>(it.id)];
                        }
                    }
                    result.solution = std::move(sol);
                    return result;
                }
            }
        }
    }





    // OCaml-like bound stop on the reduced core.
    //
    // This is attempted before any sequence/DP machinery.  It is driven by a
    // work estimate: when the reduced core can plausibly be closed cheaper than
    // the half-split DP prefix, try an exact bounded enumeration.  If it closes,
    // return immediately, matching PYAsUKP's "THE BOUND STOPPED THE COMPUTATION"
    // behavior.  If it does not close within budget, the result is ignored.
    {
        Weight core_wmax = 0;
        long long feasible_items = 0;
        for (const Item& it : items) {
            if (it.w > 0 && it.w <= inst.capacity) {
                core_wmax = std::max(core_wmax, it.w);
                ++feasible_items;
            }
        }
        if (core_wmax <= 0) core_wmax = 1;

        const Weight prefix =
            std::min<Weight>(inst.capacity, ((inst.capacity + 1) / 2) + core_wmax);

        const long double estimated_half_work =
            static_cast<long double>(std::max<Weight>(1, prefix)) *
            static_cast<long double>(std::max<long long>(1, feasible_items));

        const bool core_closure_is_cheap =
            estimated_half_work <= 5'000'000.0L ||
            (global_bound.upper > 0 &&
             static_cast<long double>(std::max<Profit>(0, global_bound.upper - incumbent)) /
             static_cast<long double>(global_bound.upper) <= 0.0001L);

        if (core_closure_is_cheap) {
            const long long core_budget = static_cast<long long>(
                std::min<long double>(50'000.0L,
                    std::max<long double>(2'000.0L, estimated_half_work / 4.0L)));

            ExactCoreClosureResult closed =
                try_exact_core_bound_stop(inst, items, global_bound.upper, incumbent,
                                          incumbent_mult, core_budget);

            result.stats.bb_nodes += closed.nodes;
            if (closed.closed) {
                result.solution = std::move(closed.solution);
                return result;
            }
        }
    }

    // Dynamic reduced-core exact path.
    //
    // This replaces the bad behavior where a very reduced core still paid for
    // half-split/sequence infrastructure.  The decision is not a fixed item
    // threshold: it compares estimated exact full-DP work with the structural
    // prefix work that the half-split would have to do.  If the reduced core is
    // cheap enough to solve directly, do so and return.
    {
        Weight reduced_wmax = 0;
        long long feasible_items = 0;
        for (const Item& it : items) {
            if (it.w > 0 && it.w <= inst.capacity) {
                reduced_wmax = std::max(reduced_wmax, it.w);
                ++feasible_items;
            }
        }
        if (reduced_wmax <= 0) reduced_wmax = 1;

        const Weight reduced_prefix =
            std::min<Weight>(inst.capacity, ((inst.capacity + 1) / 2) + reduced_wmax);

        const long double full_work =
            static_cast<long double>(std::max<Weight>(1, inst.capacity)) *
            static_cast<long double>(std::max<long long>(1, feasible_items));
        const long double half_work =
            static_cast<long double>(std::max<Weight>(1, reduced_prefix)) *
            static_cast<long double>(std::max<long long>(1, feasible_items));

        const bool reduced_core_is_cheaper =
            full_work <= std::max<long double>(2'000'000.0L, 1.35L * half_work);

        if (reduced_core_is_cheaper) {
            const long long exact_budget = static_cast<long long>(
                std::min<long double>(50'000'000.0L, std::max<long double>(2'000'000.0L, 2.0L * full_work)));

            ExactReducedCoreResult exact =
                solve_reduced_core_exact(inst, items, exact_budget);

            result.stats.states_scanned += exact.states_scanned;
            if (exact.solved) {
                result.solution = std::move(exact.solution);
                return result;
            }
        }
    }

    // ---------------------------------------------------------------------
    // Dynamic OCaml-like backend controller.
    //
    // PYAsUKP does not choose a path using a fixed item-count cutoff.  It lets
    // the structure of the sequence/slice computation decide: if the sequence is
    // sparse and progresses quickly, the sequence backend is useful; if it starts
    // behaving like a dense table, the algorithm should not keep paying that
    // overhead.
    //
    // This controller therefore runs only short probes of the sequence/slice
    // backends.  A probe may finish the instance exactly.  If it does not, the
    // solver does not spend a large fixed budget there; it falls through to the
    // exact half-split hot loop with PYAsUKP threshold dominance.  Correctness is
    // unchanged because an unfinished probe is ignored.
    // ---------------------------------------------------------------------
    {
        Weight probe_wmax = 0;
        Weight probe_wmin = inst.capacity;
        for (const Item& it : items) {
            probe_wmax = std::max(probe_wmax, it.w);
            probe_wmin = std::min(probe_wmin, it.w);
        }
        if (probe_wmax <= 0) probe_wmax = 1;
        if (probe_wmin <= 0 || probe_wmin > inst.capacity) probe_wmin = 1;

        const Weight probe_prefix_limit =
            std::min<Weight>(inst.capacity, ((inst.capacity + 1) / 2) + probe_wmax);

        const Weight probe_slice_height = std::max<Weight>(
            1,
            std::max<Weight>(probe_wmin,
                             std::min<Weight>(32768,
                                              std::max<Weight>(4096, probe_wmax / 8))));

        // Probe budget is capacity-relative, not item-count-relative.  This is a
        // dynamic guard against dense sequence behavior: if the sequence backend
        // cannot solve with a small amount of work relative to the prefix, it is
        // not behaving like PYAsUKP's compact sequence on this instance.
        const long long prefix_scale = static_cast<long long>(
            std::max<Weight>(1, std::min<Weight>(probe_prefix_limit, 2'000'000)));
        const long long slice_probe_budget =
            std::max<long long>(64'000, std::min<long long>(5'000'000, 4 * prefix_scale));
        const long long seq_probe_budget =
            std::max<long long>(64'000, std::min<long long>(4'000'000, 3 * prefix_scale));

        const std::size_t probe_points =
            static_cast<std::size_t>(std::min<Weight>(probe_prefix_limit + 1, 750'000));

        SliceMergeHalfResult merge =
            try_slice_one_merge_half_split(inst, items, probe_prefix_limit,
                                           probe_slice_height,
                                           slice_probe_budget,
                                           probe_points);

        result.stats.states_scanned += merge.states_scanned;
        if (merge.solved) {
            result.stats.states_kept += merge.critical_points;
            result.solution = std::move(merge.solution);
            return result;
        }

        // Try the queue-driven sequence probe only if the slice probe did not
        // already exhaust a dense amount of work.  This avoids the pathological
        // behavior observed on highly reduced instances, where speculative
        // sequence attempts were more expensive than the exact small-core DP.
        if (merge.states_scanned < slice_probe_budget / 2) {
            SequenceDrivenHalfResult seq =
                try_sequence_driven_half_split(inst, items, probe_prefix_limit,
                                               seq_probe_budget,
                                               probe_points);

            result.stats.states_scanned += seq.states_scanned;
            if (seq.solved) {
                result.stats.states_kept += seq.critical_points;
                result.solution = std::move(seq.solution);
                return result;
            }
        }
    }

    // ---------------------------------------------------------------------
    // EDUK2-inspired optimized DP phase.
    //
    // This version preserves the previous adaptive B&B/bound policy, but
    // replaces the full-capacity dense DP by a half-capacity split in the
    // spirit of EDUK2/PYAsUKP's c/2 stopping behavior.  The solver computes
    // exact DP values only up to
    //
    //     L = ceil(c/2) + wmax(active items),
    //
    // then composes two partial solutions with capacities a and b such that
    // a + b <= c.  This is exact for UKP: any feasible solution of total
    // weight <= c can be partitioned into two submultisets whose weights are
    // both <= ceil(c/2)+wmax, because each item has weight at most wmax.
    //
    // The implementation also keeps:
    //   - sequence_result-like critical points;
    //   - last contribution per item;
    //   - slice/layer execution;
    //   - PYAsUKP threshold dominance at slice boundaries;
    //   - sparse backtracking metadata for critical points;
    //   - contiguous arrays for cache efficiency.
    // ---------------------------------------------------------------------

    std::vector<Item> dp_items = items;
    std::sort(dp_items.begin(), dp_items.end(),
        [](const Item& a, const Item& b) {
            if (a.w != b.w) return a.w < b.w;
            return better_ratio(a, b);
        });

    // Cache-friendly representation for the hot DP loop.
    //
    // The EDUK2/PYAsUKP mechanisms above still determine the algorithmic flow:
    // c/2 stopping, sequence attempts, slices, threshold dominance, and sparse
    // backtracking.  This block only changes the physical representation used by
    // the fallback exact DP.  It keeps the current semantics, but avoids repeated
    // Item object loads and the branch `it.w > y` inside the innermost loop.
    std::vector<Weight> item_w;
    std::vector<Profit> item_p;
    std::vector<int> item_id;
    item_w.reserve(dp_items.size());
    item_p.reserve(dp_items.size());
    item_id.reserve(dp_items.size());
    for (const Item& it : dp_items) {
        item_w.push_back(it.w);
        item_p.push_back(it.p);
        item_id.push_back(it.id);
    }

    const Item best_periodic = ctx.best;

    Weight wmax_active = 0;
    Weight wmin_active = inst.capacity;
    for (const Item& it : dp_items) {
        wmax_active = std::max(wmax_active, it.w);
        wmin_active = std::min(wmin_active, it.w);
    }
    if (wmin_active <= 0 || wmin_active > inst.capacity) wmin_active = 1;
    if (wmax_active <= 0) wmax_active = 1;

    const Weight half_capacity = (inst.capacity + 1) / 2;
    const Weight compute_limit_w = std::min<Weight>(inst.capacity, half_capacity + wmax_active);
    const size_t cap = static_cast<size_t>(compute_limit_w);

    std::vector<Profit> dp(cap + 1, 0);
    std::vector<int> last(cap + 1, -1);
    std::vector<Weight> prev(cap + 1, 0);

    const std::size_t id_count = inst.items.size();
    std::vector<unsigned char> active_by_id(id_count, 1);
    std::vector<unsigned char> active_local(dp_items.size(), 1);
    bool has_inactive_local = false;
    std::vector<Weight> last_contribution_by_id(id_count, 0);
    std::vector<long long> contribution_count_by_id(id_count, 0);

    // Runtime instrumentation for comparison with PYAsUKP.
    //
    // PYAsUKP reports dynamic quantities such as "Remaining undominated items"
    // and "Not collectively dominated items" after the slice/dominance process.
    // items_after_preprocess is static, so it should not be compared directly.
    long long active_items_final = static_cast<long long>(dp_items.size());
    long long not_collectively_dominated_final = static_cast<long long>(dp_items.size());
    long long dynamic_collective_removed = 0;
    long long period_last_contribution_hits = 0;

    // The last-contribution observation is useful instrumentation, but it is
    // not by itself a replacement certificate for a capacity-indexed DP.  In
    // particular, an item that has not won a state in one slice can still be
    // required as the predecessor of a later capacity.  Treating that
    // observation as a global deletion caused exnsdbis10.ukp to lose a feasible
    // solution of value 1,028,035 and return 1,028,030 instead.
    //
    // Keep the exact DP item set unchanged until a formal contextual
    // dominance certificate is implemented.  Likewise, do not use the
    // last-contribution observation to stop for periodicity.  The final c/2
    // composition below remains exact with the complete reduced item set.
    constexpr bool kEnableUncertifiedDynamicReduction = false;
    constexpr bool kEnableUncertifiedPeriodStop = false;

    auto rebuild_compact_active_items = [&]() {
        std::vector<Item> compact;
        compact.reserve(dp_items.size());
        for (const Item& it : dp_items) {
            if (it.id >= 0 && static_cast<std::size_t>(it.id) < active_by_id.size() &&
                active_by_id[static_cast<std::size_t>(it.id)]) {
                compact.push_back(it);
            }
        }
        dp_items.swap(compact);
        item_w.clear();
        item_p.clear();
        item_id.clear();
        item_w.reserve(dp_items.size());
        item_p.reserve(dp_items.size());
        item_id.reserve(dp_items.size());
        for (const Item& it : dp_items) {
            item_w.push_back(it.w);
            item_p.push_back(it.p);
            item_id.push_back(it.id);
        }
        active_local.assign(dp_items.size(), 1);
        has_inactive_local = false;
    };

    struct CriticalPoint {
        Weight w = 0;
        Profit p = 0;
        Weight prev_w = 0;
        int last_item = -1;
    };

    std::vector<CriticalPoint> sequence_result;
    sequence_result.reserve(static_cast<std::size_t>(
        std::min<Weight>(compute_limit_w + 1, 2'000'000)));
    sequence_result.push_back(CriticalPoint{0, 0, 0, -1});

    std::unordered_map<Weight, CriticalPoint> sparse_pred;
    sparse_pred.reserve(1 << 20);
    sparse_pred.emplace(0, CriticalPoint{0, 0, 0, -1});

    // PYAsUKP threshold dominance.
    //
    // OCaml dominance.ml:
    //   threshold_test last_contribution weight capacity =
    //       last_contribution + weight <= capacity
    //
    // Interpretation: once item k has not contributed in a whole interval of
    // length at least w_k up to the current slice upper bound, it is threshold
    // dominated and never needs to be used again.  This is the key dynamic
    // reduction that the earlier C++ versions were missing.
    auto threshold_dominated_dynamic = [&](const Item& it, Weight capacity) -> bool {
        if (it.id < 0 || static_cast<std::size_t>(it.id) >= last_contribution_by_id.size()) {
            return false;
        }
        const Weight last_contribution = last_contribution_by_id[static_cast<std::size_t>(it.id)];
        return safe_add(last_contribution, it.w) <= capacity;
    };

    auto active_count = [&]() -> long long {
        long long count = 0;
        for (const Item& it : dp_items) {
            if (it.id >= 0 && static_cast<std::size_t>(it.id) < active_by_id.size() &&
                active_by_id[static_cast<std::size_t>(it.id)]) {
                ++count;
            }
        }
        return count;
    };

    auto refresh_dynamic_item_counters = [&]() {
        active_items_final = active_count();
        not_collectively_dominated_final = active_items_final;
    };

    auto recompute_active_wmax = [&]() {
        Weight wm = 0;
        for (const Item& it : dp_items) {
            if (it.id >= 0 && static_cast<std::size_t>(it.id) < active_by_id.size() &&
                active_by_id[static_cast<std::size_t>(it.id)]) {
                wm = std::max(wm, it.w);
            }
        }
        wmax_active = std::max<Weight>(1, wm);
    };

    // Periodicity by last contribution.
    //
    // Conservative PYAsUKP-like certificate: after a finalized slice yb, if
    // every active non-best item has completed at least one own-weight interval
    // without contributing to the envelope, the suffix can be completed with the
    // best item periodically.  This may detect the period earlier than the
    // contiguous best-item window certificate.
    auto period_by_last_contribution = [&](Weight yb) -> bool {
        if (yb < safe_add(wmax_active, best_periodic.w)) {
            return false;
        }

        for (const Item& it : dp_items) {
            if (it.id < 0) continue;
            const std::size_t id = static_cast<std::size_t>(it.id);
            if (id >= active_by_id.size() || !active_by_id[id]) continue;
            if (it.id == best_periodic.id) continue;

            const Weight last_y = last_contribution_by_id[id];

            if (contribution_count_by_id[id] == 0) {
                if (yb < safe_add(wmax_active, it.w)) {
                    return false;
                }
                continue;
            }

            if (safe_add(last_y, it.w) > yb) {
                return false;
            }
        }

        return true;
    };

    const Weight slice_height = std::max<Weight>(1, wmin_active);

    std::vector<unsigned char> uses_best(cap + 1, 0);
    long long periodic_window_count = 0;
    Weight periodic_window = std::max<Weight>(1, wmax_active + 1);
    Weight period_level = -1;
    Weight computed_until = compute_limit_w;

    // Residue-class periodicity certificate.
    //
    // The existing window certificate requires a full contiguous window where
    // every capacity y satisfies:
    //     dp[y] = dp[y - w_best] + p_best.
    //
    // This equivalent residue-based monitor can certify the same phenomenon
    // earlier in practice.  For each residue modulo w_best, it counts consecutive
    // successful applications of the best item recurrence.  Once every residue
    // has been stable for enough consecutive cycles to cover the active maximum
    // item weight, the remaining capacities are periodic and can be completed
    // with copies of the best item.
    //
    // Correctness is preserved because stopping is allowed only after this
    // explicit recurrence certificate is observed on finalized DP values.
    const Weight best_w_period = std::max<Weight>(1, best_periodic.w);
    const long long residue_required_streak =
        std::max<long long>(1, (wmax_active + best_w_period - 1) / best_w_period + 1);
    std::vector<int> residue_streak(static_cast<std::size_t>(best_w_period), 0);
    std::vector<unsigned char> residue_certified(static_cast<std::size_t>(best_w_period), 0);
    long long certified_residues = 0;

    long long initial_interval = 65536;
    long long bound_call_budget = 0;
    if (!skip_hybrid && structurally_bb_friendly) {
        initial_interval = 8192;
        bound_call_budget = 8192;
    } else if (!skip_hybrid && enable_dp_bound_sampling) {
        initial_interval = 65536;
        bound_call_budget = 1024;
    }
    BoundSampler sampler(ctx, inst.capacity, incumbent, initial_interval, bound_call_budget);

    Weight ya = 0;
    while (ya < compute_limit_w) {
        const Weight yb = std::min<Weight>(compute_limit_w, ya + slice_height);

        std::size_t usable_items = 0;
        while (usable_items < item_w.size() && item_w[usable_items] <= ya) {
            ++usable_items;
        }

        for (Weight y = ya + 1; y <= yb; ++y) {
            while (usable_items < item_w.size() && item_w[usable_items] <= y) {
                ++usable_items;
            }

            Profit best_profit_y = dp[static_cast<size_t>(y)];
            int best_last_y = last[static_cast<size_t>(y)];
            Weight best_prev_y = prev[static_cast<size_t>(y)];

            if (!has_inactive_local) {
                for (std::size_t ii = 0; ii < usable_items; ++ii) {
                    const Weight wi = item_w[ii];
                    const Profit cand = safe_add(dp[static_cast<size_t>(y - wi)], item_p[ii]);
                    if (cand > best_profit_y) {
                        best_profit_y = cand;
                        best_last_y = item_id[ii];
                        best_prev_y = y - wi;
                    }
                    result.stats.states_scanned++;
                }
            } else {
                for (std::size_t ii = 0; ii < usable_items; ++ii) {
                    if (!active_local[ii]) continue;
                    const Weight wi = item_w[ii];
                    const Profit cand = safe_add(dp[static_cast<size_t>(y - wi)], item_p[ii]);
                    if (cand > best_profit_y) {
                        best_profit_y = cand;
                        best_last_y = item_id[ii];
                        best_prev_y = y - wi;
                    }
                    result.stats.states_scanned++;
                }
            }

            const bool changed = (best_profit_y != dp[static_cast<size_t>(y)] ||
                                  best_last_y != last[static_cast<size_t>(y)]);

            dp[static_cast<size_t>(y)] = best_profit_y;
            last[static_cast<size_t>(y)] = best_last_y;
            prev[static_cast<size_t>(y)] = best_prev_y;
            if (best_profit_y > incumbent) incumbent = best_profit_y;

            if (changed && best_last_y >= 0) {
                CriticalPoint cp{y, best_profit_y, best_prev_y, best_last_y};
                sequence_result.push_back(cp);
                sparse_pred[y] = cp;

                const std::size_t lid = static_cast<std::size_t>(best_last_y);
                if (lid < last_contribution_by_id.size()) {
                    last_contribution_by_id[lid] = y;
                    ++contribution_count_by_id[lid];
                }
            }

            if (options_.use_bounds && enable_dp_bound_sampling) {
                sampler.incumbent = incumbent;
                sampler.observe(y, best_profit_y);
            }

            bool has_optimal_with_best = false;
            if (y >= best_periodic.w) {
                const Profit with_best =
                    safe_add(dp[static_cast<size_t>(y - best_periodic.w)], best_periodic.p);
                has_optimal_with_best = (with_best == best_profit_y);
            }

            uses_best[static_cast<size_t>(y)] = has_optimal_with_best ? 1 : 0;
            periodic_window_count += uses_best[static_cast<size_t>(y)];

            if (y >= periodic_window) {
                periodic_window_count -= uses_best[static_cast<size_t>(y - periodic_window)];
            }

            if (has_optimal_with_best && y >= best_w_period) {
                const std::size_t r = static_cast<std::size_t>(y % best_w_period);
                if (residue_streak[r] < std::numeric_limits<int>::max()) {
                    ++residue_streak[r];
                }
                if (!residue_certified[r] &&
                    residue_streak[r] >= residue_required_streak) {
                    residue_certified[r] = 1;
                    ++certified_residues;
                }
            } else if (best_w_period > 0) {
                const std::size_t r = static_cast<std::size_t>(y % best_w_period);
                if (r < residue_streak.size()) {
                    residue_streak[r] = 0;
                    if (residue_certified[r]) {
                        residue_certified[r] = 0;
                        --certified_residues;
                    }
                }
            }

            if (kEnableUncertifiedPeriodStop && periodic_window > 0 &&
                y >= periodic_window &&
                periodic_window_count == static_cast<long long>(periodic_window)) {
                period_level = y;
                computed_until = y;
                break;
            }

            if (kEnableUncertifiedPeriodStop && best_w_period > 0 &&
                certified_residues == static_cast<long long>(best_w_period) &&
                y >= periodic_window) {
                period_level = y;
                computed_until = y;
                break;
            }
        }

        if (period_level >= 0) break;

        bool removed_any = false;
        for (const Item& it : dp_items) {
            if (it.id < 0 || static_cast<std::size_t>(it.id) >= active_by_id.size()) continue;
            const std::size_t id = static_cast<std::size_t>(it.id);
            if (!active_by_id[id]) continue;
            if (kEnableUncertifiedDynamicReduction && threshold_dominated_dynamic(it, yb)) {
                active_by_id[id] = 0;
                ++dynamic_collective_removed;
                const std::size_t local = static_cast<std::size_t>(&it - dp_items.data());
                if (local < active_local.size()) {
                    active_local[local] = 0;
                    has_inactive_local = true;
                }
                removed_any = true;
            }
        }

        if (removed_any) {
            rebuild_compact_active_items();
            recompute_active_wmax();
            periodic_window = std::max<Weight>(1, wmax_active + 1);
        }

        refresh_dynamic_item_counters();

        if (kEnableUncertifiedPeriodStop && period_by_last_contribution(yb)) {
            ++period_last_contribution_hits;
            period_level = yb;
            computed_until = yb;
            break;
        }

        if (kEnableUncertifiedPeriodStop && active_items_final <= 1 && yb >= best_periodic.w) {
            period_level = yb;
            computed_until = yb;
            break;
        }

        ya = yb;
    }

    result.stats.bound_calls += sampler.calls;
    result.stats.states_fathomed += sampler.fathomed;
    result.stats.states_kept += static_cast<long long>(sequence_result.size());

    refresh_dynamic_item_counters();

    // Optional diagnostics, disabled by default to keep CLI/tests unchanged.
    // Run with:
    //   UKP_DEBUG_ACTIVE=1 ./ukp_solve optimized <instance>
    //
    // Printed to stderr so stdout remains compatible with the current parser.
    if (std::getenv("UKP_DEBUG_ACTIVE") != nullptr) {
        std::cerr
            << "debug_active_items_final " << active_items_final << '\n'
            << "debug_not_collectively_dominated_final " << not_collectively_dominated_final << '\n'
            << "debug_dynamic_collective_removed " << dynamic_collective_removed << '\n'
            << "debug_period_last_contribution_hits " << period_last_contribution_hits << '\n'
            << "debug_sequence_result_size " << sequence_result.size() << '\n';
    }

    auto add_trace = [&](Solution& sol, Weight start_y) {
        Weight y = start_y;
        while (y > 0) {
            int id = -1;
            Weight py = 0;

            auto sp = sparse_pred.find(y);
            if (sp != sparse_pred.end() && sp->second.last_item >= 0) {
                id = sp->second.last_item;
                py = sp->second.prev_w;
            } else if (y <= compute_limit_w && last[static_cast<size_t>(y)] >= 0) {
                id = last[static_cast<size_t>(y)];
                py = prev[static_cast<size_t>(y)];
            } else {
                break;
            }

            const auto it = std::find_if(inst.items.begin(), inst.items.end(),
                [&](const Item& x) { return x.id == id; });
            if (it == inst.items.end()) throw std::runtime_error("backtracking failed");

            sol.multiplicity_by_id[static_cast<std::size_t>(id)]++;
            sol.weight += it->w;
            y = py;
        }
    };

    Solution sol;
    sol.weight = 0;
    sol.optimal = true;
    sol.solver_name = "optimized";
    sol.multiplicity_by_id.assign(inst.items.size(), 0);

    // If a strict period was certified before the half split limit, complete by
    // adding copies of the best item.  Otherwise, compose two partial DP
    // solutions a+b<=c using only the prefix [0,L].
    if (period_level >= 0 && inst.capacity > period_level) {
        long long added_best = (inst.capacity - period_level + best_periodic.w - 1) / best_periodic.w;
        Weight reconstruction_capacity = inst.capacity - added_best * best_periodic.w;
        if (reconstruction_capacity < 0 || reconstruction_capacity > computed_until) {
            reconstruction_capacity = compute_limit_w;
            added_best = 0;
        }

        sol.profit = safe_add(dp[static_cast<size_t>(reconstruction_capacity)],
                              safe_mul(added_best, best_periodic.p));
        if (added_best > 0 && best_periodic.id >= 0 &&
            static_cast<std::size_t>(best_periodic.id) < sol.multiplicity_by_id.size()) {
            sol.multiplicity_by_id[static_cast<std::size_t>(best_periodic.id)] += added_best;
            sol.weight += safe_mul(added_best, best_periodic.w);
        }
        add_trace(sol, reconstruction_capacity);
    } else {
        Weight best_a = 0;
        Weight best_b = 0;
        Profit best_split_profit = 0;

        for (Weight a = 0; a <= compute_limit_w; ++a) {
            const Weight remaining = inst.capacity - a;
            const Weight b = std::min<Weight>(compute_limit_w, std::max<Weight>(0, remaining));
            const Profit val = safe_add(dp[static_cast<size_t>(a)], dp[static_cast<size_t>(b)]);
            if (val > best_split_profit) {
                best_split_profit = val;
                best_a = a;
                best_b = b;
            }
        }

        sol.profit = best_split_profit;
        add_trace(sol, best_a);
        add_trace(sol, best_b);
    }

    result.solution = std::move(sol);
    return result;
}

}  // namespace ukp::optimized
