#pragma once

#include "ukp/stats_mode.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace ukp {

// Selection policy, deliberately distinct from BoundType (the formula used).
enum class BoundPolicy { U3, V, TauStar, BestItemStar, BestCertified };

using Weight = long long;
using Profit = long long;

struct Item {
    int id = -1;
    Weight w = 0;
    Profit p = 0;
};

struct Instance {
    Weight capacity = 0;
    std::vector<Item> items;
};

struct Solution {
    Profit profit = 0;
    Weight weight = 0;
    std::vector<long long> multiplicity_by_id;
    bool optimal = false;
    std::string solver_name;
};

struct SliceStats {
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
    std::string contextual_bound_used = "none";
};

struct Stats {
    long long original_items = 0;
    long long after_preprocess_items = 0;
    long long states_scanned = 0;
    long long states_expanded = 0;
    long long successor_attempts = 0;
    long long successor_item_scans = 0;
    long long backfill_attempts = 0;
    long long cursor_advances = 0;
    long long historical_states_avoided = 0;
    long long candidates_stored = 0;
    long long computed_window_collisions = 0;
    long long computed_window_replacements = 0;
    long long computed_window_rejections = 0;
    long long computed_window_index_collisions = 0;
    long long active_item_samples = 0;
    long long active_items_sum = 0;
    long long active_items_max = 0;
    long long states_kept = 0;
    long long states_fathomed = 0;
    long long bound_calls = 0;
    long long periodicity_hits = 0;
    long long bb_nodes = 0;
    long long core_node_limit = 0;
    long long items_removed_simple = 0;
    long long items_removed_multiple = 0;
    long long items_removed_modular = 0;
    long long items_removed_core_multiple = 0;
    long long items_removed_bound = 0;
    long long items_removed_threshold = 0;
    long long points_generated = 0;
    long long incumbent_improvements_bb = 0;
    long long incumbent_improvements_dp = 0;
    long long active_items_final = 0;
    long long items_considered_for_introduction = 0;
    long long items_introduced = 0;
    long long items_rejected_by_envelope = 0;
    long long items_rejected_by_bound = 0;
    std::array<long long, 10> items_introduced_by_capacity_decile{};
    std::array<long long, 10> items_introduced_by_reduction_decile{};
    long long contextual_bound_state_queries = 0;
    long long contextual_bound_item_queries = 0;
    long long contextual_bound_calls_avoided_by_lower = 0;
    long long contextual_bound_state_calls_avoided_by_lower = 0;
    long long contextual_bound_item_calls_avoided_by_lower = 0;
    std::map<std::string, long long> contextual_bound_evaluations;
    std::map<std::string, long long> contextual_bound_wins;
    std::map<std::string, long long> contextual_bound_state_wins;
    std::map<std::string, long long> contextual_bound_item_wins;
    std::map<std::string, long long> contextual_bound_fathoms;
    long long bound_context_rebuilds = 0;
    long long bound_context_items_processed = 0;
    long long bound_context_tau_q_recomputations = 0;
    long long bound_context_tau_q_items_scanned = 0;
    long long bound_context_best_q_recomputations = 0;
    long long bound_context_best_q_items_scanned = 0;
    long long bound_context_alpha_recomputations = 0;
    long long bound_context_alpha_items_scanned = 0;
    long long bound_context_dominance_full_searches = 0;
    long long bound_context_dominance_searches_avoided_by_witness = 0;
    long long bound_context_dominance_witness_invalidations = 0;
    long long bound_context_dominance_pair_checks = 0;
    // Full-mode residual transaction telemetry.
    long long residual_transactions = 0;
    long long residual_items_removed = 0;
    long long context_rebuilds_requested = 0;
    long long context_rebuilds_skipped_no_change = 0;
    long long suffix_rebuilds = 0;
    long long duplicate_removal_requests = 0;
    // Full-mode wall-clock phase timings. They remain zero in None/Basic.
    long long phase_preprocessing_ns = 0;
    long long phase_global_bounds_ns = 0;
    long long phase_core_bb_ns = 0;
    long long phase_dp_ns = 0;
    long long phase_reconstruction_ns = 0;
    long long estimated_state_bytes = 0;
    std::string bound_winner = "none";
    std::string global_bound_used = "none";
    std::map<std::string, long long> contextual_bound_calls;
    std::vector<SliceStats> slices;
    std::string dp_stop_reason = "not_started";
    Weight periodicity_level = -1;
    Weight dp_capacity_processed = 0;
    long long active_items_at_periodicity = 0;
    std::string stop_reason = "uninitialized";
    // Test/debug telemetry.  These identify the immutable post-global-
    // reduction DP list and the local B&B core selected from it.
    std::vector<int> dp_item_ids;
    std::vector<int> core_item_ids;
    // Indexed by the original item id.  Entries for items that never become
    // active remain -1, distinguishing them from an introduced item that did
    // not need historical backfill.
    std::vector<long long> backfill_attempts_by_item;
};

struct SolverOptions {
    bool paper_faithful_mode = true;
    bool use_simple_dominance = false;
    bool use_core_remainder_ordering = false;
    bool use_modular_dominance = false;
    bool use_core_multiple_dominance = false;
    bool use_bounds = true;
    bool use_core_bb = true;
    bool use_periodicity = true;
    bool trace = false;
    BoundPolicy bound_policy = BoundPolicy::BestCertified;
    // Experimental-only.  In paper_faithful_mode the core size is
    // min(n, max(100, n / 100)) and this value is ignored.
    int core_size = -1;
    // Experimental-only.  In paper_faithful_mode B&B always uses 10,000
    // nodes and this value is ignored.
    long long bb_node_limit = 10000;
    // Zero uses PYAsUKP's executable default max(100, lightest weight).
    Weight slice_height = 0;
};

struct SolverResult {
    Solution solution;
    Stats stats;
};

inline bool better_ratio(const Item& a, const Item& b) {
    __int128 lhs = static_cast<__int128>(a.p) * b.w;
    __int128 rhs = static_cast<__int128>(b.p) * a.w;
    if (lhs != rhs) return lhs > rhs;
    if (a.w != b.w) return a.w < b.w;
    return a.id < b.id;
}

inline Profit floor_mul_div(Weight a, Profit b, Weight d) {
    if (d <= 0) throw std::invalid_argument("division by non-positive weight");
    Profit product = 0;
    if (__builtin_mul_overflow(a, b, &product)) {
        if ((a < 0) != (b < 0)) {
            throw std::overflow_error("profit underflow in floor_mul_div");
        }
        throw std::overflow_error("profit overflow in floor_mul_div");
    }
    return product / d;
}

inline Profit safe_add(Profit a, Profit b) {
    Profit result = 0;
    if (__builtin_add_overflow(a, b, &result)) {
        if (b < 0) throw std::overflow_error("profit underflow");
        throw std::overflow_error("profit overflow");
    }
    return result;
}

inline Profit safe_mul(long long a, Profit b) {
    Profit result = 0;
    if (__builtin_mul_overflow(a, b, &result)) {
        if ((a < 0) != (b < 0)) throw std::overflow_error("profit underflow");
        throw std::overflow_error("profit overflow");
    }
    return result;
}

}  // namespace ukp