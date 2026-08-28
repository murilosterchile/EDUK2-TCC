#include "preprocessing.hpp"

#include "ukp/dominance.hpp"

#include <stdexcept>

namespace ukp::optimized::detail {

std::vector<Item> common_preprocess_items(const Instance& instance) {
    std::vector<Item> items;
    items.reserve(instance.items.size());
    for (const Item& item : instance.items) {
        if (item.w < 0) {
            throw std::invalid_argument("negative item weight");
        }
        if (item.w == 0) {
            if (item.p > 0) {
                throw std::invalid_argument(
                    "unbounded UKP: positive-profit item has zero weight");
            }
            continue;
        }
        if (item.p <= 0 || item.w > instance.capacity) continue;
        items.push_back(item);
    }
    return items;
}

std::vector<Item> tso_preprocess_items(
    std::vector<Item> common_items, bool use_multiple_dominance) {
    if (!use_multiple_dominance || common_items.empty()) return common_items;

    const Item best = *std::max_element(
        common_items.begin(), common_items.end(),
        [](const Item& a, const Item& b) { return better_ratio(b, a); });
    return remove_multiple_dominated_by_best(std::move(common_items), best);
}

PreprocessResult preprocess_items_for_eduk2(
    const std::vector<Item>& common_items, bool use_simple_dominance) {
    PreprocessResult result;
    // Stage 1's reference item belongs to the working instance, not to an
    // intermediate reduction.  In particular, simple dominance is optional
    // and must never change the item used for multiple dominance.
    const Item best = *std::max_element(
        common_items.begin(), common_items.end(),
        [](const Item& a, const Item& b) { return better_ratio(b, a); });
    result.best_item = best;
    std::vector<Item>& items = result.items;
    items = common_items;
    if (use_simple_dominance) {
        const auto before_simple = items.size();
        items = remove_simple_dominated(std::move(items));
        result.simple_removed = static_cast<long long>(before_simple - items.size());
    }
    // EDUK2 stage 1: best-item multiple dominance is mandatory, including in
    // paper-derived mode; it is distinct from core-local multiple dominance.
    const auto before_multiple = items.size();
    items = remove_multiple_dominated_by_best(std::move(items), best);
    result.multiple_removed = static_cast<long long>(before_multiple - items.size());
    std::sort(items.begin(), items.end(), better_ratio);
    return result;
}

std::vector<Item> reduce_variables_by_bound(
    const std::vector<Item>& items,
    const BoundContext& context,
    Weight capacity,
    Profit incumbent,
    long long& bound_calls,
    BoundPolicy policy,
    BoundDecisionTelemetry* decision_telemetry) {
    std::vector<Item> reduced;
    reduced.reserve(items.size());
    for (const Item& item : items) {
        if (item.id == context.best.id) {
            reduced.push_back(item);
            continue;
        }
        if (item.w > capacity) continue;

        const BoundDecision decision = evaluate_candidate(
            context, item.w, item.p, capacity, incumbent, policy);
        accumulate_bound_decision_telemetry(decision_telemetry, decision);
        if (decision.evaluated_mask != 0) ++bound_calls;
        if (!decision.can_fathom) reduced.push_back(item);
    }
    return reduced;
}

}  // namespace ukp::optimized::detail
