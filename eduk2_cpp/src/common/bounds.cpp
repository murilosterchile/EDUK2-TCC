#include "ukp/bounds.hpp"
#include <cmath>

namespace ukp {

namespace {

struct Rational {
    Profit numerator = 0;
    Weight denominator = 1;
};

bool greater(const Rational& a, const Rational& b) {
    return static_cast<__int128>(a.numerator) * b.denominator >
           static_cast<__int128>(b.numerator) * a.denominator;
}

Profit floor_div(__int128 numerator, Weight denominator) {
    if (denominator <= 0) throw std::invalid_argument("non-positive rational denominator");
    const __int128 value = numerator / denominator;
    if (value > std::numeric_limits<Profit>::max()) throw std::overflow_error("bound overflow");
    return static_cast<Profit>(value);
}

// q_k* from the EDUK2 paper.  Items with no remainder modulo w_k do not
// constrain q: after multiple-dominance preprocessing their numerator is <= 0.
Rational q_star(const std::vector<Item>& items, const Item& base) {
    Rational best{0, 1};
    for (const Item& item : items) {
        if (item.id == base.id) continue;
        const Weight copies = item.w / base.w;
        const Weight remainder = item.w - copies * base.w;
        const Profit numerator = item.p - safe_mul(copies, base.p);
        if (remainder <= 0 || numerator <= 0) continue;
        const Rational candidate{numerator, remainder};
        if (greater(candidate, best)) best = candidate;
    }
    return best;
}

BoundValue bound_from_q(const Item& base, Weight c, Rational q, BoundType type) {
    const Weight copies = c / base.w;
    const __int128 numerator = static_cast<__int128>(q.numerator) * c +
        (static_cast<__int128>(base.p) * q.denominator -
         static_cast<__int128>(q.numerator) * base.w) * copies;
    return {floor_div(numerator, q.denominator), safe_mul(copies, base.p), type};
}

}  // namespace

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

    Profit alpha_num = 0;
    Weight alpha_den = 1;
    if (ctx.delta1 > 0) {
        for (const auto& it : items) {
            if (it.id == ctx.lightest_positive.id) continue;
            if (it.w < ctx.lightest_positive.w) continue;
            Profit delta_i = static_cast<Profit>(static_cast<__int128>(ctx.psi) * it.p - it.w);
            long long q = it.w / ctx.lightest_positive.w;
            if (q <= 0) continue;
            const Weight denominator = safe_mul(q, ctx.delta1);
            if (static_cast<__int128>(delta_i) * alpha_den >
                static_cast<__int128>(alpha_num) * denominator) {
                alpha_num = delta_i;
                alpha_den = denominator;
            }
        }
    }
    ctx.alpha_num = alpha_num;
    ctx.alpha_den = alpha_den;
    ctx.preferred = static_cast<__int128>(alpha_num) <= alpha_den ? BoundType::V : BoundType::Both;
    // The q* bounds in the paper assume that no item is multiply dominated.
    // Certify that condition before allowing either bound into the policy.
    bool no_multiple_dominance = true;
    for (std::size_t i = 0; i < items.size() && no_multiple_dominance; ++i) {
        for (std::size_t j = 0; j < items.size(); ++j) {
            if (i == j || items[i].w > items[j].w) continue;
            const Weight copies = items[j].w / items[i].w;
            if (copies > 0 && safe_mul(copies, items[i].p) >= items[j].p) {
                no_multiple_dominance = false;
                break;
            }
        }
    }
    ctx.tau_star_certified = no_multiple_dominance;
    ctx.best_item_star_certified = no_multiple_dominance;
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
    Profit alpha_num = ctx.alpha_num;
    Weight alpha_den = ctx.alpha_den;
    if (static_cast<__int128>(alpha_num) < alpha_den) {
        alpha_num = 1;
        alpha_den = 1;
    }
    const __int128 numerator = static_cast<__int128>(c) * alpha_den * ctx.psi +
        static_cast<__int128>(alpha_num) * xb * ctx.delta1;
    const __int128 denominator = static_cast<__int128>(alpha_den) * ctx.psi;
    if (numerator > static_cast<__int128>(std::numeric_limits<Profit>::max()) * denominator) {
        throw std::overflow_error("V bound overflow");
    }
    Profit upper = static_cast<Profit>(numerator / denominator);
    Profit lower = safe_mul(xb, m.p);
    return {upper, lower, BoundType::V};
}

BoundValue compute_tau_star(const BoundContext& ctx, Weight c) {
    const Item& base = ctx.lightest_positive;
    Rational q = q_star(ctx.ratio_items, base);
    // tau_1* = min(1, q_1*).
    if (greater(q, Rational{1, 1})) q = {1, 1};
    return bound_from_q(base, c, q, BoundType::TauStar);
}

BoundValue compute_best_item_star(const BoundContext& ctx, Weight c) {
    return bound_from_q(ctx.best, c, q_star(ctx.ratio_items, ctx.best),
                        BoundType::BestItemStar);
}

BoundValue compute_bound(const BoundContext& ctx, Weight c) {
    if (c <= 0) return {0, 0, ctx.preferred};
    BoundValue mt = compute_u3(ctx, c);
    BoundValue v = compute_v(ctx, c);
    BoundValue out = mt;
    std::vector<BoundValue> candidates{v};
    for (const BoundValue candidate : candidates) {
        if (candidate.upper < out.upper) out = candidate;
        out.lower = std::max(out.lower, candidate.lower);
    }
    return out;
}

}  // namespace ukp
