#pragma once

#include "ukp/types.hpp"
#include <array>

namespace ukp {

enum class BoundType { U3, V, TauStar, BestItemStar, Both };

inline const char* bound_type_name(BoundType type) {
    switch (type) {
        case BoundType::U3: return "U3";
        case BoundType::V: return "V";
        case BoundType::TauStar: return "TauStar";
        case BoundType::BestItemStar: return "BestItemStar";
        case BoundType::Both: return "none";
    }
    return "none";
}

struct BoundValue {
    Profit upper = 0;
    Profit lower = 0;
    BoundType type = BoundType::U3;
};

struct BoundContext {
    // Exactly the items of the residual instance being bounded.
    std::vector<Item> items;
    std::vector<Item> normalized_ratio_items;
    Item best{};
    Item second{};
    Item third{};
    Item lightest_positive{};
    Item tau_star_base{};
    Item best_item_star_base{};
    // q* bases retain the normalized profit used by the rational formula.
    Item normalized_tau_star_base{};
    Item normalized_best_item_star_base{};
    // Exact q* values, independent of the residual capacity.
    Profit tau_star_q_star_num = 0;
    Weight tau_star_q_star_den = 1;
    int tau_star_q_star_item_id = -1;
    Profit best_item_star_q_star_num = 0;
    Weight best_item_star_q_star_den = 1;
    int best_item_star_q_star_item_id = -1;
    bool has_three = false;
    bool has_lightest_positive = false;
    bool tau_normalized = false;
    // alpha is kept as an exact non-negative rational alpha_num/alpha_den.
    Profit alpha_num = 0;
    Weight alpha_den = 1;
    int alpha_item_id = -1;
    int psi = 1;
    Profit delta1 = 0;
    BoundType preferred = BoundType::Both;
    bool no_multiple_dominance = false;
    // When multiple dominance is present, these stable item IDs retain the
    // pair that certified it in the most recent full search.
    int multiple_dominance_dominator_id = -1;
    int multiple_dominance_dominated_id = -1;
    // Certified bounds in the stable selection order.  This avoids rebuilding
    // an allocating vector for every contextual BestCertified query.
    std::array<BoundType, 4> certified_types{};
    std::size_t certified_type_count = 0;
};

// Optional diagnostic sink. It records context-maintenance work without
// changing any bound formula, witness selection, or cache-validity decision.
struct BoundContextTelemetry {
    long long rebuilds = 0;
    long long items_processed = 0;
    long long tau_q_recomputations = 0;
    long long tau_q_items_scanned = 0;
    long long best_q_recomputations = 0;
    long long best_q_items_scanned = 0;
    long long alpha_recomputations = 0;
    long long alpha_items_scanned = 0;
    long long dominance_full_searches = 0;
    long long dominance_searches_avoided_by_witness = 0;
    long long dominance_witness_invalidations = 0;
    long long dominance_pair_checks = 0;
};

BoundContext make_bound_context(const std::vector<Item>& items,
                                BoundContextTelemetry* telemetry = nullptr);
// Rebuilds an existing context for a monotonically shrinking residual subset,
// retaining vector storage and cached witnesses that are still present.  The
// caller guarantees exact better_ratio order; multiplying every profit by the
// same positive psi preserves that order.
void rebuild_bound_context_ratio_ordered(BoundContext& context,
                                         const std::vector<Item>& ratio_ordered_items,
                                         BoundContextTelemetry* telemetry = nullptr);
BoundValue compute_u3(const BoundContext& ctx, Weight c);
BoundValue compute_v(const BoundContext& ctx, Weight c);
BoundValue compute_tau_star(const BoundContext& ctx, Weight c);
BoundValue compute_best_item_star(const BoundContext& ctx, Weight c);
bool is_bound_applicable(const BoundContext& ctx, BoundType type);
bool is_bound_certified(const BoundContext& ctx, BoundType type);
std::vector<BoundType> certified_bound_types(const BoundContext& ctx);
BoundValue compute_bound(const BoundContext& ctx, Weight c, BoundPolicy policy);
// Compatibility policy: historical U3/V selection. New faithful call sites
// must use the explicit-policy overload.
BoundValue compute_bound(const BoundContext& ctx, Weight c);

}  // namespace ukp
