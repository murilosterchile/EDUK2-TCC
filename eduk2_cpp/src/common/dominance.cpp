#include "ukp/dominance.hpp"
#include <algorithm>

namespace ukp {

std::vector<Item> remove_simple_dominated(std::vector<Item> items) {
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.w != b.w) return a.w < b.w;
        if (a.p != b.p) return a.p > b.p;
        return a.id < b.id;
    });
    std::vector<Item> out;
    out.reserve(items.size());
    Profit best_profit = -1;
    for (const auto& it : items) {
        if (it.p > best_profit) {
            out.push_back(it);
            best_profit = it.p;
        }
    }
    return out;
}

std::vector<Item> remove_multiple_dominated_by_best(std::vector<Item> items, const Item& best) {
    std::vector<Item> out;
    out.reserve(items.size());
    for (const auto& it : items) {
        if (it.id == best.id) {
            out.push_back(it);
            continue;
        }
        long long copies = it.w / best.w;
        bool dominated = copies > 0 &&
            copies * best.w <= it.w &&
            safe_mul(copies, best.p) >= it.p;
        if (!dominated) out.push_back(it);
    }
    return out;
}

bool threshold_dominated_by_best(const Item& item, const Item& best, Weight capacity) {
    if (item.id == best.id || best.w <= 0) return false;
    if (capacity < item.w) return false;
    // Conservative version of threshold dominance: it only removes an item when
    // one copy can be replaced by copies of the best item. This preserves
    // correctness and mirrors the cheap test used in preprocessing.
    long long copies = item.w / best.w;
    return copies > 0 && copies * best.w <= item.w && safe_mul(copies, best.p) >= item.p;
}

}  // namespace ukp
