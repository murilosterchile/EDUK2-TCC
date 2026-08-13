#include "ukp/bounds.hpp"

#include <optional>

namespace ukp {
namespace {
struct Rational { Profit numerator = 0; Weight denominator = 1; };
bool greater(const Rational& a, const Rational& b) {
    return static_cast<__int128>(a.numerator) * b.denominator >
           static_cast<__int128>(b.numerator) * a.denominator;
}
Profit floor_div(__int128 n, __int128 d) {
    if (d <= 0) throw std::invalid_argument("non-positive rational denominator");
    const __int128 v = n / d;
    if (v > std::numeric_limits<Profit>::max() || v < std::numeric_limits<Profit>::min())
        throw std::overflow_error("bound overflow");
    return static_cast<Profit>(v);
}
Rational q_star(const std::vector<Item>& items, const Item& base) {
    Rational best{0, 1};
    for (const Item& item : items) {
        if (item.id == base.id) continue;
        const Weight copies = item.w / base.w;
        const Weight remainder = item.w - copies * base.w;
        const __int128 n = static_cast<__int128>(item.p) - static_cast<__int128>(copies) * base.p;
        if (remainder <= 0 || n <= 0) continue;
        if (n > std::numeric_limits<Profit>::max()) throw std::overflow_error("q* overflow");
        Rational candidate{static_cast<Profit>(n), remainder};
        if (greater(candidate, best)) best = candidate;
    }
    return best;
}
BoundValue normalized_bound_from_q(const Item& normalized_base, const Item& original_base,
                                   Weight c, Rational q, int psi, BoundType type) {
    const Weight copies = c / normalized_base.w;
    const __int128 normalized_upper = static_cast<__int128>(q.numerator) * c +
        (static_cast<__int128>(normalized_base.p) * q.denominator -
         static_cast<__int128>(q.numerator) * normalized_base.w) * copies;
    // A normalized objective is psi times the original objective. Flooring is
    // conservative because the original objective is integral.
    return {floor_div(normalized_upper, static_cast<__int128>(q.denominator) * psi),
            safe_mul(copies, original_base.p), type};
}
bool has_no_multiple_dominance(const std::vector<Item>& items) {
    for (std::size_t i = 0; i < items.size(); ++i) for (std::size_t j = 0; j < items.size(); ++j) {
        if (i == j || items[i].w > items[j].w) continue;
        const Weight copies = items[j].w / items[i].w;
        if (copies > 0 && static_cast<__int128>(copies) * items[i].p >= items[j].p) return false;
    }
    return true;
}
Profit diff(const Item& it) { return it.p - static_cast<Profit>(it.w); }
BoundValue individual(const BoundContext& c, Weight cap, BoundType type) {
    switch (type) {
        case BoundType::U3: return compute_u3(c, cap);
        case BoundType::V: return compute_v(c, cap);
        case BoundType::TauStar: return compute_tau_star(c, cap);
        case BoundType::BestItemStar: return compute_best_item_star(c, cap);
        case BoundType::Both: break;
    }
    throw std::invalid_argument("invalid bound type");
}
BoundType policy_type(BoundPolicy p) {
    switch (p) {
        case BoundPolicy::U3: return BoundType::U3;
        case BoundPolicy::V: return BoundType::V;
        case BoundPolicy::TauStar: return BoundType::TauStar;
        case BoundPolicy::BestItemStar: return BoundType::BestItemStar;
        case BoundPolicy::BestCertified: return BoundType::Both;
    }
    return BoundType::U3;
}
} // namespace

BoundContext make_bound_context(const std::vector<Item>& items) {
    if (items.empty()) throw std::invalid_argument("empty item set");
    BoundContext ctx;
    ctx.items = items;
    // ratio_items used to be a full sorted copy solely to select these three
    // items. Keep the exact better_ratio ordering, without allocating or
    // sorting a second copy of the residual instance.
    ctx.best = items.front();
    std::optional<Item> second;
    std::optional<Item> third;
    for (std::size_t i = 1; i < items.size(); ++i) {
        const Item& candidate = items[i];
        if (better_ratio(candidate, ctx.best)) {
            third = second;
            second = ctx.best;
            ctx.best = candidate;
        } else if (!second || better_ratio(candidate, *second)) {
            third = second;
            second = candidate;
        } else if (!third || better_ratio(candidate, *third)) {
            third = candidate;
        }
    }
    ctx.best_item_star_base = ctx.best;
    if (second) ctx.second = *second;
    if (third) { ctx.third = *third; ctx.has_three = true; }

    bool found = false;
    for (const Item& it : items) if (diff(it) > 0 && (!found || it.w < ctx.lightest_positive.w ||
            (it.w == ctx.lightest_positive.w && it.id < ctx.lightest_positive.id))) {
        ctx.lightest_positive = it; found = true;
    }
    // The paper uses the ratio-best item as item 1 when no p_i-w_i is positive.
    ctx.tau_star_base = found ? ctx.lightest_positive : ctx.best;
    ctx.lightest_positive = ctx.tau_star_base;
    ctx.has_lightest_positive = true;
    ctx.psi = 1;
    if (ctx.tau_star_base.p <= ctx.tau_star_base.w) {
        const __int128 quotient = ctx.tau_star_base.w / ctx.tau_star_base.p;
        if (quotient >= std::numeric_limits<int>::max()) throw std::overflow_error("psi overflow");
        ctx.psi = static_cast<int>(quotient + 1);
        ctx.tau_normalized = true;
    }
    ctx.delta1 = floor_div(static_cast<__int128>(ctx.psi) * ctx.tau_star_base.p - ctx.tau_star_base.w, 1);
    ctx.normalized_ratio_items = items;
    for (Item& it : ctx.normalized_ratio_items)
        it.p = floor_div(static_cast<__int128>(it.p) * ctx.psi, 1);
    std::sort(ctx.normalized_ratio_items.begin(), ctx.normalized_ratio_items.end(), better_ratio);
    ctx.normalized_tau_star_base = {ctx.tau_star_base.id, ctx.tau_star_base.w,
        floor_div(static_cast<__int128>(ctx.tau_star_base.p) * ctx.psi, 1)};
    ctx.normalized_best_item_star_base = {ctx.best_item_star_base.id, ctx.best_item_star_base.w,
        floor_div(static_cast<__int128>(ctx.best_item_star_base.p) * ctx.psi, 1)};
    const Rational tau_q = q_star(ctx.normalized_ratio_items, ctx.normalized_tau_star_base);
    ctx.tau_star_q_star_num = tau_q.numerator;
    ctx.tau_star_q_star_den = tau_q.denominator;
    const Rational best_item_q = q_star(ctx.normalized_ratio_items,
                                        ctx.normalized_best_item_star_base);
    ctx.best_item_star_q_star_num = best_item_q.numerator;
    ctx.best_item_star_q_star_den = best_item_q.denominator;

    Profit alpha_num = 0; Weight alpha_den = 1;
    for (const Item& it : ctx.normalized_ratio_items) {
        if (it.id == ctx.tau_star_base.id || it.w < ctx.tau_star_base.w) continue;
        const Profit delta = it.p - static_cast<Profit>(it.w);
        const Weight copies = it.w / ctx.tau_star_base.w;
        if (copies <= 0) continue;
        const __int128 den = static_cast<__int128>(copies) * ctx.delta1;
        if (static_cast<__int128>(delta) * alpha_den > static_cast<__int128>(alpha_num) * den) {
            alpha_num = delta; alpha_den = floor_div(den, 1);
        }
    }
    ctx.alpha_num = alpha_num; ctx.alpha_den = alpha_den;
    ctx.preferred = static_cast<__int128>(alpha_num) <= alpha_den ? BoundType::V : BoundType::Both;
    ctx.no_multiple_dominance = has_no_multiple_dominance(ctx.items);
    for (const BoundType type : {BoundType::U3, BoundType::V, BoundType::TauStar,
                                 BoundType::BestItemStar}) {
        if (is_bound_certified(ctx, type))
            ctx.certified_types[ctx.certified_type_count++] = type;
    }
    return ctx;
}

bool is_bound_applicable(const BoundContext& ctx, BoundType type) {
    if (ctx.items.empty()) return false;
    switch (type) {
        case BoundType::U3: case BoundType::V: return ctx.best.w > 0;
        case BoundType::TauStar: return ctx.tau_star_base.w > 0 && ctx.tau_star_base.p > 0;
        case BoundType::BestItemStar: return ctx.best_item_star_base.w > 0 && ctx.best_item_star_base.p > 0;
        case BoundType::Both: return false;
    }
    return false;
}
bool is_bound_certified(const BoundContext& ctx, BoundType type) {
    if (!is_bound_applicable(ctx, type)) return false;
    if (type == BoundType::TauStar) {
        // The q* expression is exposed for the published SAW calculation, but
        // this context does not retain the SAW witness required to certify it.
        // Do not use it for pruning until that witness is available.
        return false;
    }
    if (type == BoundType::BestItemStar) return ctx.no_multiple_dominance;
    return true;
}
std::vector<BoundType> certified_bound_types(const BoundContext& ctx) {
    std::vector<BoundType> out;
    out.reserve(ctx.certified_type_count);
    for (std::size_t i = 0; i < ctx.certified_type_count; ++i)
        out.push_back(ctx.certified_types[i]);
    return out;
}

BoundValue compute_u3(const BoundContext& ctx, Weight c) {
    if (c <= 0) return {0, 0, BoundType::U3};
    if (!ctx.has_three) return {floor_mul_div(c, ctx.best.p, ctx.best.w), safe_mul(c / ctx.best.w, ctx.best.p), BoundType::U3};
    const Item& b1=ctx.best; const Item& b2=ctx.second; const Item& b3=ctx.third;
    Weight cb=c%b1.w, x1=c/b1.w, c2=cb%b2.w, x2=cb/b2.w;
    Profit z=safe_add(safe_mul(x1,b1.p),safe_mul(x2,b2.p));
    Profit u0=safe_add(z,floor_mul_div(c2,b3.p,b3.w));
    Weight k=(b2.w-c2+b1.w-1)/b1.w;
    Profit ub1=safe_add(safe_add(z,floor_mul_div(c2+k*b1.w,b2.p,b2.w)),-safe_mul(k,b1.p));
    return {std::max(u0,ub1),safe_add(z,safe_mul(c2/b3.w,b3.p)),BoundType::U3};
}
BoundValue compute_v(const BoundContext& ctx, Weight c) {
    if (c <= 0) return {0, 0, BoundType::V};
    const Item& m=ctx.tau_star_base; Weight xb=c/m.w;
    Profit an=ctx.alpha_num; Weight ad=ctx.alpha_den;
    if (static_cast<__int128>(an)<ad) { an=1; ad=1; }
    const __int128 n=static_cast<__int128>(c)*ad*ctx.psi+static_cast<__int128>(an)*xb*ctx.delta1;
    return {floor_div(n,static_cast<__int128>(ad)*ctx.psi),safe_mul(xb,m.p),BoundType::V};
}
BoundValue compute_tau_star(const BoundContext& ctx, Weight c) {
    Rational q{ctx.tau_star_q_star_num, ctx.tau_star_q_star_den};
    if (greater(q,{1,1})) q={1,1};
    return normalized_bound_from_q(ctx.normalized_tau_star_base, ctx.tau_star_base,
                                   c, q, ctx.psi, BoundType::TauStar);
}
BoundValue compute_best_item_star(const BoundContext& ctx, Weight c) {
    const Rational q{ctx.best_item_star_q_star_num, ctx.best_item_star_q_star_den};
    return normalized_bound_from_q(ctx.normalized_best_item_star_base,
        ctx.best_item_star_base, c, q, ctx.psi, BoundType::BestItemStar);
}
BoundValue compute_bound(const BoundContext& ctx, Weight c, BoundPolicy policy) {
    if (c <= 0) return {0,0,BoundType::U3};
    const BoundType requested=policy_type(policy);
    if (requested != BoundType::Both && is_bound_certified(ctx,requested)) return individual(ctx,c,requested);
    // Forced q* policies that are not certified fall back to certified U3;
    // the returned type always describes that fallback, never the request.
    if (ctx.certified_type_count == 0) throw std::logic_error("no certified bound");
    BoundValue out=individual(ctx,c,ctx.certified_types.front());
    if (requested != BoundType::Both) return out;
    // Stable ordering in certified_bound_types is the documented tie break.
    for (std::size_t i=1;i<ctx.certified_type_count;++i) {
        BoundValue candidate=individual(ctx,c,ctx.certified_types[i]);
        if (candidate.upper < out.upper) out=candidate;
    }
    return out;
}
BoundValue compute_bound(const BoundContext& ctx, Weight c) {
    // Historical API semantics, retained for optimized solver compatibility:
    // select between U3 and V only. Faithful always calls the policy overload.
    if (c <= 0) return {0, 0, BoundType::U3};
    BoundValue out = compute_u3(ctx, c);
    BoundValue v = compute_v(ctx, c);
    if (v.upper < out.upper) out = v;
    out.lower = std::max(out.lower, v.lower);
    return out;
}
} // namespace ukp
