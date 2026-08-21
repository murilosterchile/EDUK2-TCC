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

BoundPhase initialize_bounds(const std::vector<Item>& items, Weight capacity,
                             BoundPolicy policy,
                             BoundContextTelemetry* telemetry = nullptr);
ContextualBound compute_contextual_bound(const BoundContext& context,
                                         Weight residual_capacity,
                                         BoundPolicy policy,
                                         long long best_copies);

}  // namespace ukp::faithful::detail
