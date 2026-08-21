#include "eduk2_bounds.hpp"

namespace ukp::faithful::detail {
namespace {

constexpr std::uint8_t bound_mask(BoundType type) {
    switch (type) {
        case BoundType::U3: return 1U << 0;
        case BoundType::V: return 1U << 1;
        case BoundType::TauStar: return 1U << 2;
        case BoundType::BestItemStar: return 1U << 3;
        case BoundType::Both: return 0;
    }
    return 0;
}

Profit floor_div(__int128 numerator, __int128 denominator) {
    if (denominator <= 0) {
        throw std::invalid_argument("non-positive rational denominator");
    }
    const __int128 value = numerator / denominator;
    if (value > std::numeric_limits<Profit>::max() ||
        value < std::numeric_limits<Profit>::min()) {
        throw std::overflow_error("bound overflow");
    }
    return static_cast<Profit>(value);
}

BoundType requested_type(BoundPolicy policy) {
    switch (policy) {
        case BoundPolicy::U3: return BoundType::U3;
        case BoundPolicy::V: return BoundType::V;
        case BoundPolicy::TauStar: return BoundType::TauStar;
        case BoundPolicy::BestItemStar: return BoundType::BestItemStar;
        case BoundPolicy::BestCertified: return BoundType::Both;
    }
    return BoundType::U3;
}

ContextualBound compute_contextual_u3(const BoundContext& context,
                                      Weight capacity, long long best_copies) {
    if (capacity <= 0) return {0, BoundType::U3, bound_mask(BoundType::U3)};
    const Item& best = context.best;
    const Weight best_remainder = capacity - safe_mul(best_copies, best.w);
    if (!context.has_three) {
        return {floor_mul_div(capacity, best.p, best.w), BoundType::U3,
                bound_mask(BoundType::U3)};
    }

    const Item& second = context.second;
    const Item& third = context.third;
    const Weight second_copies = best_remainder / second.w;
    const Weight second_remainder = best_remainder - safe_mul(second_copies, second.w);
    const Profit base_profit = safe_add(
        safe_mul(best_copies, best.p), safe_mul(second_copies, second.p));
    const Profit u0 = safe_add(
        base_profit, floor_mul_div(second_remainder, third.p, third.w));
    const Weight replacement =
        (second.w - second_remainder + best.w - 1) / best.w;
    const Weight replaced_capacity = safe_add(
        second_remainder, safe_mul(replacement, best.w));
    const Profit u1 = safe_add(
        safe_add(base_profit,
                 floor_mul_div(replaced_capacity, second.p, second.w)),
        -safe_mul(replacement, best.p));
    return {std::max(u0, u1), BoundType::U3, bound_mask(BoundType::U3)};
}

ContextualBound compute_contextual_v(const BoundContext& context,
                                     Weight capacity, long long best_copies) {
    if (capacity <= 0) return {0, BoundType::V, bound_mask(BoundType::V)};
    const Item& base = context.tau_star_base;
    const Weight copies = base.id == context.best.id
        ? best_copies : capacity / base.w;
    Profit alpha_numerator = context.alpha_num;
    Weight alpha_denominator = context.alpha_den;
    if (static_cast<__int128>(alpha_numerator) < alpha_denominator) {
        alpha_numerator = 1;
        alpha_denominator = 1;
    }
    const __int128 numerator =
        static_cast<__int128>(capacity) * alpha_denominator * context.psi +
        static_cast<__int128>(alpha_numerator) * copies * context.delta1;
    return {floor_div(numerator,
                      static_cast<__int128>(alpha_denominator) * context.psi),
            BoundType::V, bound_mask(BoundType::V)};
}

ContextualBound compute_contextual_q_star(const BoundContext& context,
                                          Weight capacity, long long best_copies,
                                          bool tau_star) {
    const Item& normalized_base = tau_star
        ? context.normalized_tau_star_base
        : context.normalized_best_item_star_base;
    Profit q_numerator = tau_star
        ? context.tau_star_q_star_num
        : context.best_item_star_q_star_num;
    Weight q_denominator = tau_star
        ? context.tau_star_q_star_den
        : context.best_item_star_q_star_den;
    if (tau_star && static_cast<__int128>(q_numerator) > q_denominator) {
        q_numerator = 1;
        q_denominator = 1;
    }
    const Weight copies = normalized_base.id == context.best.id
        ? best_copies : capacity / normalized_base.w;
    const __int128 normalized_upper =
        static_cast<__int128>(q_numerator) * capacity +
        (static_cast<__int128>(normalized_base.p) * q_denominator -
         static_cast<__int128>(q_numerator) * normalized_base.w) * copies;
    return {floor_div(normalized_upper,
                      static_cast<__int128>(q_denominator) * context.psi),
            tau_star ? BoundType::TauStar : BoundType::BestItemStar,
            bound_mask(tau_star ? BoundType::TauStar : BoundType::BestItemStar)};
}

ContextualBound compute_individual_contextual(const BoundContext& context,
                                              Weight capacity, BoundType type,
                                              long long best_copies) {
    switch (type) {
        case BoundType::U3:
            return compute_contextual_u3(context, capacity, best_copies);
        case BoundType::V:
            return compute_contextual_v(context, capacity, best_copies);
        case BoundType::TauStar:
            return compute_contextual_q_star(context, capacity, best_copies, true);
        case BoundType::BestItemStar:
            return compute_contextual_q_star(context, capacity, best_copies, false);
        case BoundType::Both:
            break;
    }
    throw std::invalid_argument("invalid contextual bound type");
}

}  // namespace

BoundPhase initialize_bounds(const std::vector<Item>& items, Weight capacity,
                             BoundPolicy policy,
                             BoundContextTelemetry* telemetry) {
    BoundPhase phase;
    phase.context = make_bound_context(items, telemetry);
    phase.global = compute_bound(phase.context, capacity, policy);
    phase.best_count = capacity / phase.context.best.w;
    phase.incumbent = safe_mul(phase.best_count, phase.context.best.p);
    return phase;
}

ContextualBound compute_contextual_bound(const BoundContext& context,
                                         Weight residual_capacity,
                                         BoundPolicy policy,
                                         long long best_copies) {
    if (residual_capacity <= 0) {
        return {0, BoundType::U3, bound_mask(BoundType::U3)};
    }
    const BoundType requested = requested_type(policy);
    if (requested != BoundType::Both && is_bound_certified(context, requested)) {
        return compute_individual_contextual(
            context, residual_capacity, requested, best_copies);
    }
    if (context.certified_type_count == 0) {
        throw std::logic_error("no certified bound");
    }
    ContextualBound result = compute_individual_contextual(
        context, residual_capacity, context.certified_types.front(),
        best_copies);
    if (requested != BoundType::Both) return result;
    for (std::size_t index = 1; index < context.certified_type_count; ++index) {
        const ContextualBound candidate = compute_individual_contextual(
            context, residual_capacity, context.certified_types[index],
            best_copies);
        result.evaluated_mask |= candidate.evaluated_mask;
        if (candidate.upper < result.upper) {
            const std::uint8_t evaluated_mask = result.evaluated_mask;
            result = candidate;
            result.evaluated_mask = evaluated_mask;
        }
    }
    return result;
}

}  // namespace ukp::faithful::detail
