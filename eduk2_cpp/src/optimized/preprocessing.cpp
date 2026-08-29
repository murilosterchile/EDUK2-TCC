#include "preprocessing.hpp"

#include "ukp/dominance.hpp"

#include <stdexcept>
#include <utility>

namespace ukp::optimized::detail {
namespace {

bool survives_common_preprocessing(const Instance& instance, const Item& item) {
    if (item.w < 0) throw std::invalid_argument("negative item weight");
    if (item.w == 0) {
        if (item.p > 0) {
            throw std::invalid_argument(
                "unbounded UKP: positive-profit item has zero weight");
        }
        return false;
    }
    return item.p > 0 && item.w <= instance.capacity;
}

PreprocessResult finish_eduk2_preprocessing(
    std::vector<Item> items, bool use_simple_dominance) {
    PreprocessResult result;
    if (items.empty()) return result;

    // Stage 1's reference item belongs to the feasible working instance, not
    // to an intermediate dominance reduction.  Simple dominance must never
    // change the item used by best-item multiple dominance.
    const Item best = *std::max_element(
        items.begin(), items.end(),
        [](const Item& a, const Item& b) { return better_ratio(b, a); });
    result.best_item = best;

    if (use_simple_dominance) {
        const auto before_simple = items.size();
        items = remove_simple_dominated(std::move(items));
        result.simple_removed =
            static_cast<long long>(before_simple - items.size());
    }

    const auto before_multiple = items.size();
    items = remove_multiple_dominated_by_best(std::move(items), best);
    result.multiple_removed =
        static_cast<long long>(before_multiple - items.size());
    std::sort(items.begin(), items.end(), better_ratio);
    result.items = std::move(items);
    return result;
}

}  // namespace

std::vector<Item> common_preprocess_items(const Instance& instance) {
    std::vector<Item> items;
    items.reserve(instance.items.size());
    for (const Item& item : instance.items) {
        if (survives_common_preprocessing(instance, item)) {
            items.push_back(item);
        }
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
    std::vector<Item>& common_items, bool use_simple_dominance) {
    return finish_eduk2_preprocessing(
        std::move(common_items), use_simple_dominance);
}

PreprocessResult preprocess_items_for_eduk2(
    const Instance& instance, bool use_simple_dominance) {
    // Build the actual EDUK2 vector directly.  On the normal dispatcher reject
    // path this replaces common_preprocess_items(instance) followed by a move,
    // eliminating one complete vector construction and its allocator traffic.
    std::vector<Item> items;
    items.reserve(instance.items.size());
    for (const Item& item : instance.items) {
        if (survives_common_preprocessing(instance, item)) {
            items.push_back(item);
        }
    }
    return finish_eduk2_preprocessing(std::move(items), use_simple_dominance);
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