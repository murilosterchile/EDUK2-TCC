#pragma once

#include <algorithm>
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
    long long successor_attempts = 0;
    long long states_created = 0;
    long long states_kept = 0;
    long long states_fathomed_by_bound = 0;
    long long items_removed_threshold = 0;
    long long active_items_before = 0;
    long long active_items_after = 0;
    std::string contextual_bound_used = "none";
};

struct Stats {
    long long original_items = 0;
    long long after_preprocess_items = 0;
    long long states_scanned = 0;
    long long states_kept = 0;
    long long states_fathomed = 0;
    long long bound_calls = 0;
    long long periodicity_hits = 0;
    long long bb_nodes = 0;
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
    long long estimated_state_bytes = 0;
    std::string bound_winner = "none";
    std::string global_bound_used = "none";
    std::map<std::string, long long> contextual_bound_calls;
    std::vector<SliceStats> slices;
    std::string dp_stop_reason = "not_started";
    Weight periodicity_level = -1;
    std::string stop_reason = "uninitialized";
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
    int core_size = -1;
    long long bb_node_limit = 10000;
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
    __int128 x = static_cast<__int128>(a) * b;
    if (x > std::numeric_limits<Profit>::max()) {
        throw std::overflow_error("profit overflow in floor_mul_div");
    }
    if (x < std::numeric_limits<Profit>::min()) {
        throw std::overflow_error("profit underflow in floor_mul_div");
    }
    return static_cast<Profit>(x / d);
}

inline Profit safe_add(Profit a, Profit b) {
    __int128 x = static_cast<__int128>(a) + b;
    if (x > std::numeric_limits<Profit>::max()) throw std::overflow_error("profit overflow");
    if (x < std::numeric_limits<Profit>::min()) throw std::overflow_error("profit underflow");
    return static_cast<Profit>(x);
}

inline Profit safe_mul(long long a, Profit b) {
    __int128 x = static_cast<__int128>(a) * b;
    if (x > std::numeric_limits<Profit>::max()) throw std::overflow_error("profit overflow");
    if (x < std::numeric_limits<Profit>::min()) throw std::overflow_error("profit underflow");
    return static_cast<Profit>(x);
}

}  // namespace ukp
