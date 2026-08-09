#pragma once

#include "ukp/types.hpp"
#include <optional>

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
    std::vector<Item> ratio_items;
    Item best{};
    Item second{};
    Item third{};
    Item lightest_positive{};
    bool has_three = false;
    bool has_lightest_positive = false;
    // alpha is kept as an exact non-negative rational alpha_num/alpha_den.
    Profit alpha_num = 0;
    Weight alpha_den = 1;
    int psi = 1;
    Profit delta1 = 0;
    BoundType preferred = BoundType::Both;
    bool tau_star_certified = false;
    bool best_item_star_certified = false;
};

BoundContext make_bound_context(const std::vector<Item>& items);
BoundValue compute_u3(const BoundContext& ctx, Weight c);
BoundValue compute_v(const BoundContext& ctx, Weight c);
BoundValue compute_tau_star(const BoundContext& ctx, Weight c);
BoundValue compute_best_item_star(const BoundContext& ctx, Weight c);
BoundValue compute_bound(const BoundContext& ctx, Weight c);

}  // namespace ukp
