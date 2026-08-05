#include "eduk2_bounds.hpp"

namespace ukp::faithful::detail {

BoundPhase initialize_bounds(const std::vector<Item>& items, Weight capacity) {
    BoundPhase phase;
    phase.context = make_bound_context(items);
    phase.global = compute_bound(phase.context, capacity);
    phase.best_count = capacity / phase.context.best.w;
    phase.incumbent = safe_mul(phase.best_count, phase.context.best.p);
    return phase;
}

}  // namespace ukp::faithful::detail
