#pragma once

#include "ukp/bounds.hpp"

namespace ukp::faithful::detail {

struct BoundPhase {
    BoundContext context;
    BoundValue global;
    Profit incumbent = 0;
    long long best_count = 0;
};

// Contextual DP fathoming only consumes the upper value and its witness type.
// This evaluator preserves the certified-policy ordering of compute_bound but
// reuses the already known quotient by the current ratio-best item.
struct ContextualBound {
    Profit upper = 0;
    BoundType type = BoundType::U3;
    std::uint8_t evaluated_mask = 0;
};

// Unified decision returned by every faithful variable/state reduction call.
// `can_fathom` is true exactly when a certified upper bound proves
// prefix_profit + U(total_capacity - prefix_weight) <= incumbent.
struct BoundDecision {
    bool can_fathom = false;
    Profit upper = std::numeric_limits<Profit>::max();
    BoundType witness = BoundType::Both;
    std::uint8_t evaluated_mask = 0;
    bool lower_filter_hit = false;
    bool short_circuited = false;
};

// Optional diagnostic accumulator. The solver passes it only in Full mode;
// preprocessing also accepts a null pointer so benchmark builds do not retain
// any telemetry work in the DP hot path.
struct BoundDecisionTelemetry {
    long long lower_filter_hits = 0;
    std::array<long long, 4> bounds_evaluated{};
    long long bounds_short_circuited = 0;
};

BoundPhase initialize_bounds(const std::vector<Item>& items, Weight capacity,
                             BoundPolicy policy,
                             BoundContextTelemetry* telemetry = nullptr);

ContextualBound compute_contextual_bound(const BoundContext& context,
                                         Weight residual_capacity,
                                         BoundPolicy policy,
                                         long long best_copies);

BoundDecision evaluate_candidate(const BoundContext& context,
                                 Weight prefix_weight,
                                 Profit prefix_profit,
                                 Weight total_capacity,
                                 Profit incumbent,
                                 BoundPolicy policy);

void accumulate_bound_decision_telemetry(
    BoundDecisionTelemetry* telemetry, const BoundDecision& decision) noexcept;

}  // namespace ukp::faithful::detail