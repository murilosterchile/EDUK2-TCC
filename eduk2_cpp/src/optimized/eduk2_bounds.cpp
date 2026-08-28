#include "eduk2_bounds.hpp"

namespace ukp::optimized::detail {
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

constexpr std::size_t bound_index(BoundType type) {
    switch (type) {
        case BoundType::U3: return 0;
        case BoundType::V: return 1;
        case BoundType::TauStar: return 2;
        case BoundType::BestItemStar: return 3;
        case BoundType::Both: return 4;
    }
    return 4;
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
        case BoundPolicy::BestCertified:
        case BoundPolicy::PyasukpFaithful:
        case BoundPolicy::PyasukpBoth:
            return BoundType::Both;
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
    const bool include_optional_bounds = policy != BoundPolicy::PyasukpFaithful;
    phase.context = make_bound_context(items, telemetry, include_optional_bounds);
    phase.effective_policy = policy == BoundPolicy::PyasukpFaithful
        ? resolve_pyasukp_policy(phase.context, capacity)
        : policy;
    phase.global = compute_bound(phase.context, capacity, phase.effective_policy);
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
    if (policy == BoundPolicy::PyasukpFaithful) {
        policy = resolve_pyasukp_policy(context, residual_capacity);
    }
    if (policy == BoundPolicy::PyasukpBoth) {
        ContextualBound mt = compute_individual_contextual(
            context, residual_capacity, BoundType::U3, best_copies);
        ContextualBound v = compute_individual_contextual(
            context, residual_capacity, BoundType::V, best_copies);
        const std::uint8_t evaluated = mt.evaluated_mask | v.evaluated_mask;
        ContextualBound result = v.upper <= mt.upper ? v : mt;
        result.evaluated_mask = evaluated;
        return result;
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

BoundDecision evaluate_candidate(const BoundContext& context,
                                 Weight prefix_weight,
                                 Profit prefix_profit,
                                 Weight total_capacity,
                                 Profit incumbent,
                                 BoundPolicy policy) {
    if (context.items.empty() || context.best.w <= 0) {
        throw std::invalid_argument("invalid bound context");
    }
    if (prefix_weight < 0 || prefix_weight > total_capacity) {
        throw std::invalid_argument("candidate prefix weight is outside capacity");
    }

    BoundDecision decision;
    const Weight residual_capacity = total_capacity - prefix_weight;
    const long long best_copies = residual_capacity / context.best.w;
    const Profit feasible_profit = safe_add(
        prefix_profit, safe_mul(best_copies, context.best.p));

    // Any valid upper bound must be at least this feasible completion. If it
    // already improves the incumbent, no certified upper bound can fathom the
    // candidate, so all expensive formulas are skipped exactly.
    if (feasible_profit > incumbent) {
        decision.lower_filter_hit = true;
        return decision;
    }

    if (residual_capacity <= 0) {
        decision.can_fathom = prefix_profit <= incumbent;
        decision.upper = 0;
        decision.witness = BoundType::U3;
        return decision;
    }

    if (policy == BoundPolicy::PyasukpFaithful) {
        policy = resolve_pyasukp_policy(context, total_capacity);
    }
    if (policy == BoundPolicy::PyasukpBoth) {
        const ContextualBound mt = compute_individual_contextual(
            context, residual_capacity, BoundType::U3, best_copies);
        const ContextualBound v = compute_individual_contextual(
            context, residual_capacity, BoundType::V, best_copies);
        decision.evaluated_mask = mt.evaluated_mask | v.evaluated_mask;
        const ContextualBound& winner = v.upper <= mt.upper ? v : mt;
        decision.upper = winner.upper;
        decision.witness = winner.type;
        decision.can_fathom =
            safe_add(prefix_profit, decision.upper) <= incumbent;
        return decision;
    }

    const BoundType requested = requested_type(policy);
    const bool best_certified = policy == BoundPolicy::BestCertified;
    if (context.certified_type_count == 0) {
        throw std::logic_error("no certified bound");
    }

    auto evaluate_one = [&](BoundType type, bool has_remaining) {
        const ContextualBound bound = compute_individual_contextual(
            context, residual_capacity, type, best_copies);
        decision.evaluated_mask |= bound.evaluated_mask;
        if (decision.witness == BoundType::Both || bound.upper < decision.upper) {
            decision.upper = bound.upper;
            decision.witness = bound.type;
        }
        if (safe_add(prefix_profit, bound.upper) <= incumbent) {
            decision.can_fathom = true;
            decision.upper = bound.upper;
            decision.witness = bound.type;
            decision.short_circuited = has_remaining;
            return true;
        }
        return false;
    };

    if (!best_certified) {
        const BoundType selected = is_bound_certified(context, requested)
            ? requested : context.certified_types.front();
        evaluate_one(selected, false);
        return decision;
    }

    for (std::size_t index = 0; index < context.certified_type_count; ++index) {
        const bool has_remaining = index + 1 < context.certified_type_count;
        if (evaluate_one(context.certified_types[index], has_remaining)) break;
    }
    return decision;
}

FeasibleCompletion complete_with_bound(const BoundContext& context,
                                       BoundPolicy resolved_pyasukp_policy,
                                       Weight residual_capacity) {
    if (residual_capacity <= 0) return {};
    if (resolved_pyasukp_policy == BoundPolicy::PyasukpFaithful) {
        // The solver normally resolves once at the original capacity.  Keep
        // this fallback only for direct callers/tests.
        resolved_pyasukp_policy =
            resolve_pyasukp_policy(context, residual_capacity);
    }

    auto complete_u3 = [&]() {
        FeasibleCompletion out;
        const Item& b1 = context.best;
        const long long x1 = residual_capacity / b1.w;
        Weight remainder = residual_capacity - safe_mul(x1, b1.w);
        if (x1 > 0) {
            out.terms[out.term_count++] = {b1.id, x1};
            out.profit = safe_add(out.profit, safe_mul(x1, b1.p));
            out.weight = safe_add(out.weight, safe_mul(x1, b1.w));
        }
        if (!context.has_three) return out;

        const Item& b2 = context.second;
        const long long x2 = remainder / b2.w;
        remainder -= safe_mul(x2, b2.w);
        if (x2 > 0) {
            out.terms[out.term_count++] = {b2.id, x2};
            out.profit = safe_add(out.profit, safe_mul(x2, b2.p));
            out.weight = safe_add(out.weight, safe_mul(x2, b2.w));
        }

        const Item& b3 = context.third;
        const long long x3 = remainder / b3.w;
        if (x3 > 0) {
            out.terms[out.term_count++] = {b3.id, x3};
            out.profit = safe_add(out.profit, safe_mul(x3, b3.p));
            out.weight = safe_add(out.weight, safe_mul(x3, b3.w));
        }
        return out;
    };

    auto complete_v = [&]() {
        FeasibleCompletion out;
        const Item& item = context.tau_star_base;
        const long long copies = residual_capacity / item.w;
        if (copies > 0) {
            out.terms[0] = {item.id, copies};
            out.term_count = 1;
            out.profit = safe_mul(copies, item.p);
            out.weight = safe_mul(copies, item.w);
        }
        return out;
    };

    if (resolved_pyasukp_policy == BoundPolicy::U3) return complete_u3();
    if (resolved_pyasukp_policy == BoundPolicy::V) return complete_v();
    if (resolved_pyasukp_policy == BoundPolicy::PyasukpBoth) {
        const FeasibleCompletion mt = complete_u3();
        const FeasibleCompletion v = complete_v();
        if (v.profit != mt.profit) return v.profit > mt.profit ? v : mt;
        return v.weight > mt.weight ? v : mt;
    }
    throw std::invalid_argument("bound completion requires resolved PYAsUKP policy");
}

void accumulate_bound_decision_telemetry(
    BoundDecisionTelemetry* telemetry, const BoundDecision& decision) noexcept {
    if (telemetry == nullptr) return;
    if (decision.lower_filter_hit) ++telemetry->lower_filter_hits;
    for (const BoundType type : {BoundType::U3, BoundType::V,
                                 BoundType::TauStar,
                                 BoundType::BestItemStar}) {
        const std::size_t index = bound_index(type);
        if ((decision.evaluated_mask & bound_mask(type)) != 0) {
            ++telemetry->bounds_evaluated[index];
        }
    }
    if (decision.short_circuited) ++telemetry->bounds_short_circuited;
}

}  // namespace ukp::optimized::detail