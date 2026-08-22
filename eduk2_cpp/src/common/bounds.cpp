#include "ukp/bounds.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace ukp {
namespace {
struct Rational { Profit numerator = 0; Weight denominator = 1; };
struct RationalWitness {
    Rational value{};
    int item_id = -1;
};
bool greater(const Rational& a, const Rational& b) {
    return static_cast<__int128>(a.numerator) * b.denominator >
           static_cast<__int128>(b.numerator) * a.denominator;
}
Profit floor_div(__int128 n, __int128 d) {
    if (d <= 0) throw std::invalid_argument("non-positive rational denominator");
    if (n <= std::numeric_limits<Profit>::max() &&
        n >= std::numeric_limits<Profit>::min() &&
        d <= std::numeric_limits<Weight>::max()) {
        return static_cast<Profit>(n) / static_cast<Weight>(d);
    }
    const __int128 v = n / d;
    if (v > std::numeric_limits<Profit>::max() || v < std::numeric_limits<Profit>::min())
        throw std::overflow_error("bound overflow");
    return static_cast<Profit>(v);
}
RationalWitness q_star(const std::vector<Item>& items, const Item& base) {
    RationalWitness best{{0, 1}, -1};
    for (const Item& item : items) {
        if (item.id == base.id) continue;
        const Weight copies = item.w / base.w;
        const Weight remainder = item.w - copies * base.w;
        const __int128 n = static_cast<__int128>(item.p) - static_cast<__int128>(copies) * base.p;
        if (remainder <= 0 || n <= 0) continue;
        if (n > std::numeric_limits<Profit>::max()) throw std::overflow_error("q* overflow");
        Rational candidate{static_cast<Profit>(n), remainder};
        if (greater(candidate, best.value)) best = {candidate, item.id};
    }
    return best;
}
bool same_item(const Item& left, const Item& right) {
    return left.id == right.id && left.w == right.w && left.p == right.p;
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
bool has_no_multiple_dominance(const std::vector<Item>& items,
                               int& dominator_id,
                               int& dominated_id,
                               BoundContextTelemetry* telemetry) {
    dominator_id = -1;
    dominated_id = -1;
    long long pair_checks = 0;
    for (std::size_t i = 0; i < items.size(); ++i) for (std::size_t j = 0; j < items.size(); ++j) {
        ++pair_checks;
        if (i == j || items[i].w > items[j].w) continue;
        const Weight copies = items[j].w / items[i].w;
        if (copies > 0 && static_cast<__int128>(copies) * items[i].p >= items[j].p) {
            dominator_id = items[i].id;
            dominated_id = items[j].id;
            if (telemetry != nullptr) telemetry->dominance_pair_checks += pair_checks;
            return false;
        }
    }
    if (telemetry != nullptr) telemetry->dominance_pair_checks += pair_checks;
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

bool same_item_vector(const std::vector<Item>& left, const std::vector<Item>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (!same_item(left[i], right[i])) return false;
    }
    return true;
}

bool same_bound_value(const BoundValue& left, const BoundValue& right) {
    return left.upper == right.upper && left.lower == right.lower && left.type == right.type;
}

[[noreturn]] void context_mismatch(const char* field) {
    throw std::logic_error(std::string("incremental BoundContext mismatch: ") + field);
}

void require_context_field(bool condition, const char* field) {
    if (!condition) context_mismatch(field);
}

void rebuild_normalized_items(BoundContext& ctx) {
    ctx.normalized_ratio_items.assign(ctx.items.begin(), ctx.items.end());
    for (Item& item : ctx.normalized_ratio_items) {
        item.p = floor_div(static_cast<__int128>(item.p) * ctx.psi, 1);
    }
}

void refresh_certified_types(BoundContext& ctx) {
    ctx.certified_types = {};
    ctx.certified_type_count = 0;
    for (const BoundType type : {BoundType::U3, BoundType::V, BoundType::TauStar,
                                 BoundType::BestItemStar}) {
        if (is_bound_certified(ctx, type))
            ctx.certified_types[ctx.certified_type_count++] = type;
    }
}
} // namespace

namespace {
void populate_bound_context(BoundContext& ctx, const std::vector<Item>& items,
                            bool items_are_ratio_ordered,
                            BoundContextTelemetry* telemetry) {
    if (items.empty()) throw std::invalid_argument("empty item set");
    if (telemetry != nullptr) {
        ++telemetry->rebuilds;
        telemetry->items_processed += static_cast<long long>(items.size());
    }
    const bool has_previous_subset = items_are_ratio_ordered && !ctx.items.empty();
    const Item previous_tau_base = ctx.tau_star_base;
    const Item previous_best_base = ctx.best_item_star_base;
    const int previous_psi = ctx.psi;
    const Profit previous_tau_q_num = ctx.tau_star_q_star_num;
    const Weight previous_tau_q_den = ctx.tau_star_q_star_den;
    const int previous_tau_q_item_id = ctx.tau_star_q_star_item_id;
    const Profit previous_best_q_num = ctx.best_item_star_q_star_num;
    const Weight previous_best_q_den = ctx.best_item_star_q_star_den;
    const int previous_best_q_item_id = ctx.best_item_star_q_star_item_id;
    const Profit previous_alpha_num = ctx.alpha_num;
    const Weight previous_alpha_den = ctx.alpha_den;
    const int previous_alpha_item_id = ctx.alpha_item_id;
    const bool previous_no_multiple_dominance = ctx.no_multiple_dominance;
    const int previous_multiple_dominance_dominator_id =
        ctx.multiple_dominance_dominator_id;
    const int previous_multiple_dominance_dominated_id =
        ctx.multiple_dominance_dominated_id;
    // Contexts are rebuilt repeatedly while the residual item set shrinks.
    // Preserve the two largest buffers instead of allocating fresh copies.
    std::vector<Item> item_storage = std::move(ctx.items);
    std::vector<Item> normalized_storage = std::move(ctx.normalized_ratio_items);
    ctx = BoundContext{};
    ctx.items = std::move(item_storage);
    ctx.normalized_ratio_items = std::move(normalized_storage);
    ctx.items.assign(items.begin(), items.end());
    // ratio_items used to be a full sorted copy solely to select these three
    // items. Keep the exact better_ratio ordering, without allocating or
    // sorting a second copy of the residual instance.
    ctx.best = items.front();
    std::optional<Item> second;
    std::optional<Item> third;
    if (items_are_ratio_ordered) {
        if (items.size() >= 2) second = items[1];
        if (items.size() >= 3) third = items[2];
    } else {
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
    ctx.normalized_ratio_items.assign(items.begin(), items.end());
    bool previous_tau_q_item_present = previous_tau_q_item_id < 0;
    bool previous_best_q_item_present = previous_best_q_item_id < 0;
    bool previous_alpha_item_present = previous_alpha_item_id < 0;
    bool previous_multiple_dominance_dominator_present = false;
    bool previous_multiple_dominance_dominated_present = false;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const Item& original = items[index];
        previous_tau_q_item_present |= original.id == previous_tau_q_item_id;
        previous_best_q_item_present |= original.id == previous_best_q_item_id;
        previous_alpha_item_present |= original.id == previous_alpha_item_id;
        previous_multiple_dominance_dominator_present |=
            original.id == previous_multiple_dominance_dominator_id;
        previous_multiple_dominance_dominated_present |=
            original.id == previous_multiple_dominance_dominated_id;
        Item& normalized = ctx.normalized_ratio_items[index];
        normalized.p = floor_div(static_cast<__int128>(normalized.p) * ctx.psi, 1);
    }
    if (!items_are_ratio_ordered) {
        std::sort(ctx.normalized_ratio_items.begin(), ctx.normalized_ratio_items.end(), better_ratio);
    }
    ctx.normalized_tau_star_base = {ctx.tau_star_base.id, ctx.tau_star_base.w,
        floor_div(static_cast<__int128>(ctx.tau_star_base.p) * ctx.psi, 1)};
    ctx.normalized_best_item_star_base = {ctx.best_item_star_base.id, ctx.best_item_star_base.w,
        floor_div(static_cast<__int128>(ctx.best_item_star_base.p) * ctx.psi, 1)};
    const bool tau_context_unchanged = has_previous_subset && previous_psi == ctx.psi &&
        same_item(previous_tau_base, ctx.tau_star_base);
    if (tau_context_unchanged && previous_tau_q_item_present) {
        ctx.tau_star_q_star_num = previous_tau_q_num;
        ctx.tau_star_q_star_den = previous_tau_q_den;
        ctx.tau_star_q_star_item_id = previous_tau_q_item_id;
    } else {
        if (telemetry != nullptr) {
            ++telemetry->tau_q_recomputations;
            telemetry->tau_q_items_scanned += static_cast<long long>(items.size());
        }
        const RationalWitness tau_q =
            q_star(ctx.normalized_ratio_items, ctx.normalized_tau_star_base);
        ctx.tau_star_q_star_num = tau_q.value.numerator;
        ctx.tau_star_q_star_den = tau_q.value.denominator;
        ctx.tau_star_q_star_item_id = tau_q.item_id;
    }
    const bool best_context_unchanged = has_previous_subset && previous_psi == ctx.psi &&
        same_item(previous_best_base, ctx.best_item_star_base);
    if (best_context_unchanged && previous_best_q_item_present) {
        ctx.best_item_star_q_star_num = previous_best_q_num;
        ctx.best_item_star_q_star_den = previous_best_q_den;
        ctx.best_item_star_q_star_item_id = previous_best_q_item_id;
    } else {
        if (telemetry != nullptr) {
            ++telemetry->best_q_recomputations;
            telemetry->best_q_items_scanned += static_cast<long long>(items.size());
        }
        const RationalWitness best_item_q =
            q_star(ctx.normalized_ratio_items, ctx.normalized_best_item_star_base);
        ctx.best_item_star_q_star_num = best_item_q.value.numerator;
        ctx.best_item_star_q_star_den = best_item_q.value.denominator;
        ctx.best_item_star_q_star_item_id = best_item_q.item_id;
    }

    if (tau_context_unchanged && previous_alpha_item_present) {
        ctx.alpha_num = previous_alpha_num;
        ctx.alpha_den = previous_alpha_den;
        ctx.alpha_item_id = previous_alpha_item_id;
    } else {
        if (telemetry != nullptr) {
            ++telemetry->alpha_recomputations;
            telemetry->alpha_items_scanned += static_cast<long long>(items.size());
        }
        Profit alpha_num = 0;
        Weight alpha_den = 1;
        int alpha_item_id = -1;
        for (const Item& it : ctx.normalized_ratio_items) {
            if (it.id == ctx.tau_star_base.id || it.w < ctx.tau_star_base.w) continue;
            const Profit delta = it.p - static_cast<Profit>(it.w);
            const Weight copies = it.w / ctx.tau_star_base.w;
            if (copies <= 0) continue;
            const __int128 den = static_cast<__int128>(copies) * ctx.delta1;
            if (static_cast<__int128>(delta) * alpha_den >
                static_cast<__int128>(alpha_num) * den) {
                alpha_num = delta;
                alpha_den = floor_div(den, 1);
                alpha_item_id = it.id;
            }
        }
        ctx.alpha_num = alpha_num;
        ctx.alpha_den = alpha_den;
        ctx.alpha_item_id = alpha_item_id;
    }
    ctx.preferred = static_cast<__int128>(ctx.alpha_num) <= ctx.alpha_den
        ? BoundType::V : BoundType::Both;
    if (has_previous_subset && previous_no_multiple_dominance) {
        ctx.no_multiple_dominance = true;
    } else if (has_previous_subset &&
               previous_multiple_dominance_dominator_id >= 0 &&
               previous_multiple_dominance_dominated_id >= 0 &&
               previous_multiple_dominance_dominator_present &&
               previous_multiple_dominance_dominated_present) {
        ctx.no_multiple_dominance = false;
        ctx.multiple_dominance_dominator_id =
            previous_multiple_dominance_dominator_id;
        ctx.multiple_dominance_dominated_id =
            previous_multiple_dominance_dominated_id;
        if (telemetry != nullptr) {
            ++telemetry->dominance_searches_avoided_by_witness;
        }
    } else {
        if (telemetry != nullptr) {
            ++telemetry->dominance_full_searches;
            if (has_previous_subset &&
                previous_multiple_dominance_dominator_id >= 0 &&
                previous_multiple_dominance_dominated_id >= 0) {
                ++telemetry->dominance_witness_invalidations;
            }
        }
        ctx.no_multiple_dominance = has_no_multiple_dominance(
            ctx.items, ctx.multiple_dominance_dominator_id,
            ctx.multiple_dominance_dominated_id, telemetry);
    }
    refresh_certified_types(ctx);
}

bool id_is_removed(const std::vector<unsigned char>& removed_by_id, int item_id) {
    if (item_id < 0 || static_cast<std::size_t>(item_id) >= removed_by_id.size()) {
        throw std::logic_error("BoundContext item id is outside removal table");
    }
    return removed_by_id[static_cast<std::size_t>(item_id)] != 0;
}

void compact_removed_items(std::vector<Item>& items,
                           const std::vector<unsigned char>& removed_by_id) {
    const auto new_end = std::remove_if(
        items.begin(), items.end(),
        [&](const Item& item) { return id_is_removed(removed_by_id, item.id); });
    items.erase(new_end, items.end());
}

void compute_alpha(BoundContext& ctx, BoundContextTelemetry* telemetry) {
    if (telemetry != nullptr) {
        ++telemetry->alpha_recomputations;
        telemetry->alpha_items_scanned += static_cast<long long>(ctx.normalized_ratio_items.size());
    }
    Profit alpha_num = 0;
    Weight alpha_den = 1;
    int alpha_item_id = -1;
    for (const Item& it : ctx.normalized_ratio_items) {
        if (it.id == ctx.tau_star_base.id || it.w < ctx.tau_star_base.w) continue;
        const Profit delta = it.p - static_cast<Profit>(it.w);
        const Weight copies = it.w / ctx.tau_star_base.w;
        if (copies <= 0) continue;
        const __int128 den = static_cast<__int128>(copies) * ctx.delta1;
        if (static_cast<__int128>(delta) * alpha_den >
            static_cast<__int128>(alpha_num) * den) {
            alpha_num = delta;
            alpha_den = floor_div(den, 1);
            alpha_item_id = it.id;
        }
    }
    ctx.alpha_num = alpha_num;
    ctx.alpha_den = alpha_den;
    ctx.alpha_item_id = alpha_item_id;
}

void verify_context_fields(const BoundContext& actual, const BoundContext& oracle) {
    require_context_field(same_item_vector(actual.items, oracle.items), "items");
    require_context_field(same_item_vector(actual.normalized_ratio_items,
                                           oracle.normalized_ratio_items),
                          "normalized_ratio_items");
    require_context_field(same_item(actual.best, oracle.best), "best");
    require_context_field(same_item(actual.second, oracle.second), "second");
    require_context_field(same_item(actual.third, oracle.third), "third");
    require_context_field(same_item(actual.lightest_positive, oracle.lightest_positive),
                          "lightest_positive");
    require_context_field(same_item(actual.tau_star_base, oracle.tau_star_base),
                          "tau_star_base");
    require_context_field(same_item(actual.best_item_star_base, oracle.best_item_star_base),
                          "best_item_star_base");
    require_context_field(same_item(actual.normalized_tau_star_base,
                                    oracle.normalized_tau_star_base),
                          "normalized_tau_star_base");
    require_context_field(same_item(actual.normalized_best_item_star_base,
                                    oracle.normalized_best_item_star_base),
                          "normalized_best_item_star_base");
    require_context_field(actual.tau_star_q_star_num == oracle.tau_star_q_star_num,
                          "tau_star_q_star_num");
    require_context_field(actual.tau_star_q_star_den == oracle.tau_star_q_star_den,
                          "tau_star_q_star_den");
    require_context_field(actual.tau_star_q_star_item_id == oracle.tau_star_q_star_item_id,
                          "tau_star_q_star_item_id");
    require_context_field(actual.best_item_star_q_star_num == oracle.best_item_star_q_star_num,
                          "best_item_star_q_star_num");
    require_context_field(actual.best_item_star_q_star_den == oracle.best_item_star_q_star_den,
                          "best_item_star_q_star_den");
    require_context_field(actual.best_item_star_q_star_item_id ==
                              oracle.best_item_star_q_star_item_id,
                          "best_item_star_q_star_item_id");
    require_context_field(actual.has_three == oracle.has_three, "has_three");
    require_context_field(actual.has_lightest_positive == oracle.has_lightest_positive,
                          "has_lightest_positive");
    require_context_field(actual.tau_normalized == oracle.tau_normalized,
                          "tau_normalized");
    require_context_field(actual.alpha_num == oracle.alpha_num, "alpha_num");
    require_context_field(actual.alpha_den == oracle.alpha_den, "alpha_den");
    require_context_field(actual.alpha_item_id == oracle.alpha_item_id, "alpha_item_id");
    require_context_field(actual.psi == oracle.psi, "psi");
    require_context_field(actual.delta1 == oracle.delta1, "delta1");
    require_context_field(actual.preferred == oracle.preferred, "preferred");
    require_context_field(actual.no_multiple_dominance == oracle.no_multiple_dominance,
                          "no_multiple_dominance");
    require_context_field(actual.multiple_dominance_dominator_id ==
                              oracle.multiple_dominance_dominator_id,
                          "multiple_dominance_dominator_id");
    require_context_field(actual.multiple_dominance_dominated_id ==
                              oracle.multiple_dominance_dominated_id,
                          "multiple_dominance_dominated_id");
    require_context_field(actual.certified_type_count == oracle.certified_type_count,
                          "certified_type_count");
    require_context_field(actual.certified_types == oracle.certified_types,
                          "certified_types");
}

void verify_context_bounds(const BoundContext& actual, const BoundContext& oracle) {
    Weight maximum_weight = 1;
    for (const Item& item : actual.items) maximum_weight = std::max(maximum_weight, item.w);
    const Weight doubled = maximum_weight <= (std::numeric_limits<Weight>::max() - 1) / 2
        ? maximum_weight * 2 + 1 : maximum_weight;
    const std::array<Weight, 6> capacities{
        Weight{0}, Weight{1}, actual.best.w, actual.tau_star_base.w,
        maximum_weight, doubled};
    for (const Weight capacity : capacities) {
        require_context_field(same_bound_value(compute_u3(actual, capacity),
                                               compute_u3(oracle, capacity)),
                              "U3");
        require_context_field(same_bound_value(compute_v(actual, capacity),
                                               compute_v(oracle, capacity)),
                              "V");
        require_context_field(same_bound_value(compute_tau_star(actual, capacity),
                                               compute_tau_star(oracle, capacity)),
                              "TauStar");
        require_context_field(same_bound_value(compute_best_item_star(actual, capacity),
                                               compute_best_item_star(oracle, capacity)),
                              "BestItemStar");
        for (const BoundPolicy policy : {BoundPolicy::U3, BoundPolicy::V,
                                         BoundPolicy::TauStar,
                                         BoundPolicy::BestItemStar,
                                         BoundPolicy::BestCertified}) {
            require_context_field(same_bound_value(compute_bound(actual, capacity, policy),
                                                   compute_bound(oracle, capacity, policy)),
                                  "compute_bound policy");
        }
    }
}
} // namespace

BoundContext make_bound_context(const std::vector<Item>& items,
                                BoundContextTelemetry* telemetry) {
    BoundContext ctx;
    populate_bound_context(ctx, items, false, telemetry);
    return ctx;
}

void rebuild_bound_context_ratio_ordered(BoundContext& context,
                                         const std::vector<Item>& ratio_ordered_items,
                                         BoundContextTelemetry* telemetry) {
    populate_bound_context(context, ratio_ordered_items, true, telemetry);
}

void apply_bound_context_removals(
    BoundContext& ctx,
    const std::vector<int>& removed_ids,
    const std::vector<unsigned char>& removed_by_id,
    BoundContextTelemetry* telemetry) {
    if (removed_ids.empty()) return;
    if (ctx.items.empty()) throw std::invalid_argument("empty BoundContext");

    using Clock = std::chrono::steady_clock;
    const Clock::time_point started = telemetry != nullptr ? Clock::now() : Clock::time_point{};

    // ResidualDelta already owns a dense per-ID request table. Reuse it as an
    // O(1) membership oracle while compacting the ratio-ordered vectors; this
    // avoids sorting IDs or performing a binary search for every residual item.
    for (const int item_id : removed_ids) {
        if (item_id < 0 || static_cast<std::size_t>(item_id) >= removed_by_id.size() ||
            removed_by_id[static_cast<std::size_t>(item_id)] == 0) {
            throw std::logic_error("BoundContext removal table does not match removed IDs");
        }
    }

    const std::size_t old_item_count = ctx.items.size();
    const Item previous_tau_base = ctx.tau_star_base;
    const Item previous_best_base = ctx.best_item_star_base;
    const int previous_psi = ctx.psi;
    const Profit previous_tau_q_num = ctx.tau_star_q_star_num;
    const Weight previous_tau_q_den = ctx.tau_star_q_star_den;
    const int previous_tau_q_item_id = ctx.tau_star_q_star_item_id;
    const Profit previous_best_q_num = ctx.best_item_star_q_star_num;
    const Weight previous_best_q_den = ctx.best_item_star_q_star_den;
    const int previous_best_q_item_id = ctx.best_item_star_q_star_item_id;
    const Profit previous_alpha_num = ctx.alpha_num;
    const Weight previous_alpha_den = ctx.alpha_den;
    const int previous_alpha_item_id = ctx.alpha_item_id;
    const bool previous_no_multiple_dominance = ctx.no_multiple_dominance;
    const int previous_multiple_dominance_dominator_id =
        ctx.multiple_dominance_dominator_id;
    const int previous_multiple_dominance_dominated_id =
        ctx.multiple_dominance_dominated_id;

    compact_removed_items(ctx.items, removed_by_id);
    if (ctx.items.size() == old_item_count) return;
    if (ctx.items.empty()) {
        throw std::logic_error("residual transaction removed every item");
    }
    if (telemetry != nullptr) {
        ++telemetry->incremental_updates;
        telemetry->items_processed += static_cast<long long>(old_item_count);
    }

    // Because ctx.items remains a stable better_ratio subsequence, the top
    // three witnesses are simply the first survivors. No sorting or scan is
    // needed when an earlier witness disappears.
    ctx.best = ctx.items.front();
    ctx.best_item_star_base = ctx.best;
    ctx.second = Item{};
    ctx.third = Item{};
    ctx.has_three = false;
    if (ctx.items.size() >= 2) ctx.second = ctx.items[1];
    if (ctx.items.size() >= 3) {
        ctx.third = ctx.items[2];
        ctx.has_three = true;
    }

    // A surviving lightest-positive witness remains lightest under deletion.
    // If no positive witness existed before, deletion cannot create one.
    bool found_positive = false;
    Item lightest_positive{};
    const bool previous_tau_was_positive = diff(previous_tau_base) > 0;
    if (previous_tau_was_positive &&
        !id_is_removed(removed_by_id, previous_tau_base.id)) {
        lightest_positive = previous_tau_base;
        found_positive = true;
    } else if (previous_tau_was_positive) {
        for (const Item& item : ctx.items) {
            if (diff(item) <= 0) continue;
            if (!found_positive || item.w < lightest_positive.w ||
                (item.w == lightest_positive.w && item.id < lightest_positive.id)) {
                lightest_positive = item;
                found_positive = true;
            }
        }
    }
    ctx.tau_star_base = found_positive ? lightest_positive : ctx.best;
    ctx.lightest_positive = ctx.tau_star_base;
    ctx.has_lightest_positive = true;

    ctx.psi = 1;
    ctx.tau_normalized = false;
    if (ctx.tau_star_base.p <= ctx.tau_star_base.w) {
        const __int128 quotient = ctx.tau_star_base.w / ctx.tau_star_base.p;
        if (quotient >= std::numeric_limits<int>::max()) {
            throw std::overflow_error("psi overflow");
        }
        ctx.psi = static_cast<int>(quotient + 1);
        ctx.tau_normalized = true;
    }
    ctx.delta1 = floor_div(
        static_cast<__int128>(ctx.psi) * ctx.tau_star_base.p - ctx.tau_star_base.w, 1);

    const bool tau_base_unchanged = same_item(previous_tau_base, ctx.tau_star_base);
    const bool best_base_unchanged = same_item(previous_best_base, ctx.best_item_star_base);
    const bool psi_unchanged = previous_psi == ctx.psi;
    // Keep normalized profits only while both q* bases and psi are unchanged.
    // A base/psi change takes the conservative full-normalization path.
    if (tau_base_unchanged && best_base_unchanged && psi_unchanged) {
        compact_removed_items(ctx.normalized_ratio_items, removed_by_id);
    } else {
        rebuild_normalized_items(ctx);
    }

    ctx.normalized_tau_star_base = {
        ctx.tau_star_base.id, ctx.tau_star_base.w,
        floor_div(static_cast<__int128>(ctx.tau_star_base.p) * ctx.psi, 1)};
    ctx.normalized_best_item_star_base = {
        ctx.best_item_star_base.id, ctx.best_item_star_base.w,
        floor_div(static_cast<__int128>(ctx.best_item_star_base.p) * ctx.psi, 1)};

    const bool previous_tau_q_item_survives =
        previous_tau_q_item_id < 0 ||
        !id_is_removed(removed_by_id, previous_tau_q_item_id);
    if (tau_base_unchanged && psi_unchanged && previous_tau_q_item_survives) {
        ctx.tau_star_q_star_num = previous_tau_q_num;
        ctx.tau_star_q_star_den = previous_tau_q_den;
        ctx.tau_star_q_star_item_id = previous_tau_q_item_id;
    } else {
        if (telemetry != nullptr) {
            ++telemetry->tau_q_recomputations;
            telemetry->tau_q_items_scanned +=
                static_cast<long long>(ctx.normalized_ratio_items.size());
        }
        const RationalWitness tau_q =
            q_star(ctx.normalized_ratio_items, ctx.normalized_tau_star_base);
        ctx.tau_star_q_star_num = tau_q.value.numerator;
        ctx.tau_star_q_star_den = tau_q.value.denominator;
        ctx.tau_star_q_star_item_id = tau_q.item_id;
    }

    const bool previous_best_q_item_survives =
        previous_best_q_item_id < 0 ||
        !id_is_removed(removed_by_id, previous_best_q_item_id);
    if (best_base_unchanged && psi_unchanged && previous_best_q_item_survives) {
        ctx.best_item_star_q_star_num = previous_best_q_num;
        ctx.best_item_star_q_star_den = previous_best_q_den;
        ctx.best_item_star_q_star_item_id = previous_best_q_item_id;
    } else {
        if (telemetry != nullptr) {
            ++telemetry->best_q_recomputations;
            telemetry->best_q_items_scanned +=
                static_cast<long long>(ctx.normalized_ratio_items.size());
        }
        const RationalWitness best_q =
            q_star(ctx.normalized_ratio_items, ctx.normalized_best_item_star_base);
        ctx.best_item_star_q_star_num = best_q.value.numerator;
        ctx.best_item_star_q_star_den = best_q.value.denominator;
        ctx.best_item_star_q_star_item_id = best_q.item_id;
    }

    const bool previous_alpha_item_survives =
        previous_alpha_item_id < 0 ||
        !id_is_removed(removed_by_id, previous_alpha_item_id);
    if (tau_base_unchanged && psi_unchanged && previous_alpha_item_survives) {
        ctx.alpha_num = previous_alpha_num;
        ctx.alpha_den = previous_alpha_den;
        ctx.alpha_item_id = previous_alpha_item_id;
    } else {
        compute_alpha(ctx, telemetry);
    }
    ctx.preferred = static_cast<__int128>(ctx.alpha_num) <= ctx.alpha_den
        ? BoundType::V : BoundType::Both;

    ctx.multiple_dominance_dominator_id = -1;
    ctx.multiple_dominance_dominated_id = -1;
    if (previous_no_multiple_dominance) {
        // Removing items cannot create a new multiple-dominance pair.
        ctx.no_multiple_dominance = true;
    } else {
        const bool dominator_survives = previous_multiple_dominance_dominator_id >= 0 &&
            !id_is_removed(removed_by_id, previous_multiple_dominance_dominator_id);
        const bool dominated_survives = previous_multiple_dominance_dominated_id >= 0 &&
            !id_is_removed(removed_by_id, previous_multiple_dominance_dominated_id);
        if (dominator_survives && dominated_survives) {
            ctx.no_multiple_dominance = false;
            ctx.multiple_dominance_dominator_id =
                previous_multiple_dominance_dominator_id;
            ctx.multiple_dominance_dominated_id =
                previous_multiple_dominance_dominated_id;
            if (telemetry != nullptr) {
                ++telemetry->dominance_searches_avoided_by_witness;
            }
        } else {
            if (telemetry != nullptr) {
                ++telemetry->dominance_full_searches;
                if (previous_multiple_dominance_dominator_id >= 0 &&
                    previous_multiple_dominance_dominated_id >= 0) {
                    ++telemetry->dominance_witness_invalidations;
                }
            }
            ctx.no_multiple_dominance = has_no_multiple_dominance(
                ctx.items, ctx.multiple_dominance_dominator_id,
                ctx.multiple_dominance_dominated_id, telemetry);
        }
    }

    refresh_certified_types(ctx);

    if (telemetry != nullptr) {
        telemetry->incremental_maintenance_ns +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - started).count();
    }
}

void verify_bound_context_against_full_rebuild(const BoundContext& context) {
    if (context.items.empty()) context_mismatch("empty context");
    BoundContext oracle;
    populate_bound_context(oracle, context.items, true, nullptr);
    verify_context_fields(context, oracle);
    verify_context_bounds(context, oracle);
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