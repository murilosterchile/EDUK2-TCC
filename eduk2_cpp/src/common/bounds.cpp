#include "ukp/bounds.hpp"
#include <cmath>

namespace ukp {

static Profit diff_profit_weight(const Item& it) {
    return it.p - static_cast<Profit>(it.w);
}

BoundContext make_bound_context(const std::vector<Item>& items) {
    if (items.empty()) throw std::invalid_argument("empty item set");
    BoundContext ctx;
    ctx.ratio_items = items;
    std::sort(ctx.ratio_items.begin(), ctx.ratio_items.end(), better_ratio);
    ctx.best = ctx.ratio_items[0];
    if (ctx.ratio_items.size() >= 2) ctx.second = ctx.ratio_items[1];
    if (ctx.ratio_items.size() >= 3) {
        ctx.third = ctx.ratio_items[2];
        ctx.has_three = true;
    }

    bool found = false;
    for (const auto& it : items) {
        if (diff_profit_weight(it) > 0) {
            if (!found || it.w < ctx.lightest_positive.w ||
                (it.w == ctx.lightest_positive.w && it.id < ctx.lightest_positive.id)) {
                ctx.lightest_positive = it;
                found = true;
            }
        }
    }
    if (!found) {
        ctx.lightest_positive = *std::min_element(items.begin(), items.end(),
            [](const Item& a, const Item& b) {
                if (a.w != b.w) return a.w < b.w;
                return a.id < b.id;
            });
    }
    ctx.has_lightest_positive = true;

    // Same normalization idea used in bounds.ml: choose psi so that psi*p_min - w_min > 0.
    int psi = static_cast<int>(ctx.lightest_positive.w / std::max<Profit>(1, ctx.lightest_positive.p)) + 1;
    while (static_cast<__int128>(psi) * ctx.lightest_positive.p <= ctx.lightest_positive.w) ++psi;
    ctx.psi = std::max(1, psi);
    ctx.delta1 = static_cast<Profit>(static_cast<__int128>(ctx.psi) * ctx.lightest_positive.p - ctx.lightest_positive.w);

    double alpha = 0.0;
    if (ctx.delta1 > 0) {
        for (const auto& it : items) {
            if (it.id == ctx.lightest_positive.id) continue;
            if (it.w < ctx.lightest_positive.w) continue;
            Profit delta_i = static_cast<Profit>(static_cast<__int128>(ctx.psi) * it.p - it.w);
            long long q = it.w / ctx.lightest_positive.w;
            if (q <= 0) continue;
            alpha = std::max(alpha, static_cast<double>(delta_i) /
                                      (static_cast<double>(ctx.delta1) * static_cast<double>(q)));
        }
    }
    ctx.alpha = alpha;
    ctx.preferred = (alpha <= 1.0) ? BoundType::V : BoundType::Both;
    return ctx;
}

BoundValue compute_u3(const BoundContext& ctx, Weight c) {
    if (!ctx.has_three) {
        Profit upper = floor_mul_div(c, ctx.best.p, ctx.best.w);
        Profit lower = safe_mul(c / ctx.best.w, ctx.best.p);
        return {upper, lower, BoundType::U3};
    }
    const Item& b1 = ctx.best;
    const Item& b2 = ctx.second;
    const Item& b3 = ctx.third;

    Weight cb = c % b1.w;
    Weight x1 = c / b1.w;
    Weight c2 = cb % b2.w;
    Weight x2 = cb / b2.w;
    Profit z = safe_add(safe_mul(x1, b1.p), safe_mul(x2, b2.p));

    Profit u0 = safe_add(z, floor_mul_div(c2, b3.p, b3.w));
    Weight k = (b2.w - c2 + b1.w - 1) / b1.w;
    Weight extra_capacity = c2 + k * b1.w;
    Profit ub1 = safe_add(z, floor_mul_div(extra_capacity, b2.p, b2.w));
    ub1 = safe_add(ub1, -safe_mul(k, b1.p));

    Weight x3 = c2 / b3.w;
    Profit lower = safe_add(z, safe_mul(x3, b3.p));
    return {std::max(u0, ub1), lower, BoundType::U3};
}

BoundValue compute_v(const BoundContext& ctx, Weight c) {
    const Item& m = ctx.lightest_positive;
    Weight xb = c / m.w;
    double a = std::max(1.0, ctx.alpha);
    long double term = static_cast<long double>(a) *
                       static_cast<long double>(xb) *
                       static_cast<long double>(ctx.delta1) /
                       static_cast<long double>(ctx.psi);
    Profit upper = static_cast<Profit>(std::floor(static_cast<long double>(c) + term + 1e-12L));
    Profit lower = safe_mul(xb, m.p);
    return {upper, lower, BoundType::V};
}

BoundValue compute_bound(const BoundContext& ctx, Weight c) {
    if (c <= 0) return {0, 0, ctx.preferred};
    if (ctx.preferred == BoundType::V) return compute_v(ctx, c);
    BoundValue mt = compute_u3(ctx, c);
    BoundValue v = compute_v(ctx, c);
    BoundValue out;
    out.upper = std::min(mt.upper, v.upper);
    out.lower = std::max(mt.lower, v.lower);
    out.type = (v.upper <= mt.upper) ? BoundType::V : BoundType::U3;
    return out;
}

}  // namespace ukp
