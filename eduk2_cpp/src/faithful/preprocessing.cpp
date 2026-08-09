#include "preprocessing.hpp"

#include "ukp/dominance.hpp"

namespace ukp::faithful::detail {

PreprocessResult preprocess_items(const Instance& instance, bool use_simple_dominance) {
    PreprocessResult result;
    // Stage 1's reference item belongs to the working instance, not to an
    // intermediate reduction.  In particular, simple dominance is optional
    // and must never change the item used for multiple dominance.
    const Item best = *std::max_element(
        instance.items.begin(), instance.items.end(),
        [](const Item& a, const Item& b) { return better_ratio(b, a); });
    result.best_item = best;
    std::vector<Item>& items = result.items;
    items = instance.items;
    if (use_simple_dominance) {
        const auto before_simple = items.size();
        items = remove_simple_dominated(std::move(items));
        result.simple_removed = static_cast<long long>(before_simple - items.size());
    }
    // EDUK2 stage 1: best-item multiple dominance is mandatory, including in
    // paper-faithful mode; it is distinct from core-local multiple dominance.
    const auto before_multiple = items.size();
    items = remove_multiple_dominated_by_best(std::move(items), best);
    result.multiple_removed = static_cast<long long>(before_multiple - items.size());
    std::sort(items.begin(), items.end(), better_ratio);
    return result;
}

std::vector<Item> reduce_variables_by_bound(const std::vector<Item>& items,
                                            const BoundContext& context,
                                            Weight capacity, Profit incumbent,
                                            long long& bound_calls, BoundPolicy policy) {
    std::vector<Item> reduced;
    reduced.reserve(items.size());
    for (const Item& item : items) {
        if (item.id == context.best.id) {
            reduced.push_back(item);
            continue;
        }
        if (item.w > capacity) continue;
        ++bound_calls;
        if (safe_add(item.p, compute_bound(context, capacity - item.w, policy).upper) > incumbent) {
            reduced.push_back(item);
        }
    }
    return reduced;
}

}  // namespace ukp::faithful::detail
