#include "preprocessing.hpp"

#include "ukp/dominance.hpp"

namespace ukp::faithful::detail {

PreprocessResult preprocess_items(const Instance& instance, bool enabled) {
    PreprocessResult result;
    std::vector<Item>& items = result.items;
    items = instance.items;
    if (enabled) {
        const auto before_simple = items.size();
        items = remove_simple_dominated(std::move(items));
        result.simple_removed = static_cast<long long>(before_simple - items.size());
        const Item best = *std::max_element(items.begin(), items.end(),
            [](const Item& left, const Item& right) { return better_ratio(right, left); });
        const auto before_multiple = items.size();
        items = remove_multiple_dominated_by_best(std::move(items), best);
        result.multiple_removed = static_cast<long long>(before_multiple - items.size());
    }
    std::sort(items.begin(), items.end(), better_ratio);
    return result;
}

std::vector<Item> reduce_variables_by_bound(const std::vector<Item>& items,
                                            const BoundContext& context,
                                            Weight capacity, Profit incumbent,
                                            long long& bound_calls) {
    std::vector<Item> reduced;
    reduced.reserve(items.size());
    for (const Item& item : items) {
        if (item.id == context.best.id) {
            reduced.push_back(item);
            continue;
        }
        if (item.w > capacity) continue;
        ++bound_calls;
        if (safe_add(item.p, compute_bound(context, capacity - item.w).upper) > incumbent) {
            reduced.push_back(item);
        }
    }
    return reduced;
}

}  // namespace ukp::faithful::detail
