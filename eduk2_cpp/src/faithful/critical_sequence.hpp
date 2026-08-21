#pragma once

#include "ukp/types.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace ukp::faithful::detail {

using PointId = std::size_t;
inline constexpr PointId no_point = static_cast<PointId>(-1);

// A state is kept only when it strictly raises f(N, y) over every smaller
// capacity.  `states_` is append-only so predecessor chains remain valid.
struct State {
    Weight weight = 0;
    Profit profit = 0;
    PointId predecessor = no_point;
    int item_id = -1;
};

// Hot DP representation.  tie_rank is assigned once from the immutable
// better_ratio order, avoiding an indirect id lookup for every successor.
struct ActiveItem {
    int id = -1;
    int tie_rank = 0;
    Weight w = 0;
    Profit p = 0;
};

struct SliceBuildResult {
    long long states_entered = 0;
    long long states_expanded = 0;
    long long successor_attempts = 0;
    long long successor_item_scans = 0;
    long long backfill_attempts = 0;
    long long cursor_advances = 0;
    long long states_created = 0;
    long long points_generated = 0;
    long long items_considered_for_introduction = 0;
    long long items_introduced = 0;
    long long items_rejected_by_envelope = 0;
    long long items_rejected_by_bound = 0;
    long long active_item_samples = 0;
    long long active_items_sum = 0;
    long long active_items_max = 0;
    std::array<long long, 10> items_introduced_by_capacity_decile{};
    std::array<long long, 10> items_introduced_by_reduction_decile{};
};

class CriticalSequence {
public:
    CriticalSequence();

    // Processes ]ya, yb].  The visitor decides whether an accepted state may
    // expand; this leaves bounds and other solver policy outside the DP data
    // structure.  Returning false preserves the state but emits no successors.
    template <typename ShouldExpand>
    SliceBuildResult process_slice(Weight ya, Weight yb, Weight compute_limit,
                                   const std::vector<Item>& items,
                                   ShouldExpand&& should_expand);

    // PYAsUKP's Select.next_lightest + Init.introduce counterpart.  Items are
    // supplied in nondecreasing weight order.  At weight wi, an item is made
    // active only when pi strictly raises the envelope built without it and
    // the contextual predicate accepts it.  Its transitions from earlier
    // expandable states are then backfilled before the scan advances.
    template <typename ShouldIntroduce, typename ShouldExpand>
    SliceBuildResult process_slice_incremental(
        Weight ya, Weight yb, Weight compute_limit,
        std::vector<ActiveItem>& active_items,
        const std::vector<ActiveItem>& items_by_weight,
        std::size_t& next_item,
        ShouldIntroduce&& should_introduce,
        ShouldExpand&& should_expand);

    // Establishes PYAsUKP's ratio-order tie priority independently of the
    // order in which weight-selected items become active.
    void configure_item_order(const std::vector<Item>& ratio_ordered_items);

    [[nodiscard]] PointId state_at_or_before(Weight y) const;
    [[nodiscard]] Profit value_at(Weight y) const;
    [[nodiscard]] const State& state(PointId id) const;
    [[nodiscard]] const std::vector<State>& states() const noexcept;
    [[nodiscard]] std::size_t stored_states() const noexcept;
    [[nodiscard]] long long generated_candidates() const noexcept;
    [[nodiscard]] long long candidates_stored() const noexcept;
    [[nodiscard]] long long computed_window_collisions() const noexcept;
    [[nodiscard]] long long computed_window_replacements() const noexcept;
    [[nodiscard]] long long computed_window_rejections() const noexcept;
    [[nodiscard]] long long computed_window_index_collisions() const noexcept;
    [[nodiscard]] std::size_t estimated_bytes() const noexcept;
    [[nodiscard]] std::size_t unprocessed_historical_states(
        const ActiveItem& item, Weight target_limit) const;
    [[nodiscard]] long long item_backfill_attempts(
        const ActiveItem& item) const;
    [[nodiscard]] bool item_was_introduced(const ActiveItem& item) const;
    void stop_item_after_slice(const ActiveItem& item);

private:
    struct Candidate {
        Profit profit;
        PointId predecessor;
        int item_id;
        int tie_rank;
    };

    struct ItemCursor {
        // Index in expandable_points_ corresponding to next_built_upon.
        std::size_t next_built_upon = 1;
        // Exclusive end of the prefix present at introduction.
        std::size_t historical_end = 1;
        // Exclusive parent frontier represented by this cursor. Active cursors
        // are unbounded; threshold removal seals the frontier after the slice.
        std::size_t built_upon_end = std::numeric_limits<std::size_t>::max();
        long long backfill_attempts = 0;
        bool introduced = false;
    };

    // All successors of the weight currently being visited are at most the
    // largest item weight ahead.  A circular window of at least that width
    // therefore retains every pending candidate without a capacity-sized
    // table.  Power-of-two growth makes indexing a mask operation and
    // amortizes the cost of introducing successively heavier items.
    class ComputedWindow {
    public:
        void configure(Weight largest_item_weight);
        void store(Weight weight, const Candidate& candidate);
        [[nodiscard]] bool contains(Weight weight) const;
        [[nodiscard]] Candidate take(Weight weight);
        [[nodiscard]] std::size_t estimated_bytes() const noexcept;
        [[nodiscard]] long long candidates_stored() const noexcept;
        [[nodiscard]] long long collisions() const noexcept;
        [[nodiscard]] long long replacements() const noexcept;
        [[nodiscard]] long long rejections() const noexcept;
        [[nodiscard]] long long index_collisions() const noexcept;

    private:
        struct Slot {
            Candidate candidate{};
            Weight weight = -1;
        };

        [[nodiscard]] std::size_t index(Weight weight) const;

        std::vector<Slot> slots_;
        Weight largest_item_weight_ = 0;
        std::size_t index_mask_ = 0;
        long long candidates_stored_ = 0;
        long long collisions_ = 0;
        long long replacements_ = 0;
        long long rejections_ = 0;
        long long index_collisions_ = 0;
    };

    void schedule_successors(PointId parent, Weight compute_limit,
                             const std::vector<Item>& items, SliceBuildResult& result);
    void advance_item_cursor(const ActiveItem& item, Weight target_limit,
                             SliceBuildResult& result);
    void advance_active_cursors(const std::vector<ActiveItem>& items, Weight target_limit,
                                SliceBuildResult& result);
    void schedule_current_active_successors(PointId parent, Weight target_limit,
                                            const std::vector<ActiveItem>& items,
                                            SliceBuildResult& result);
    void synchronize_active_cursors(const std::vector<ActiveItem>& items,
                                    Weight target_limit);
    static void sample_active_items(const std::vector<ActiveItem>& items,
                                    SliceBuildResult& result);
    void initialize_item_cursor(const ActiveItem& item);
    [[nodiscard]] ItemCursor& cursor_for(const ActiveItem& item);
    [[nodiscard]] const ItemCursor& cursor_for(const ActiveItem& item) const;
    void reserve_storage(Weight compute_limit, const std::vector<Item>& items);
    void reserve_active_storage(Weight compute_limit,
                                const std::vector<ActiveItem>& items);
    [[nodiscard]] int tie_rank(const Item& item, std::size_t fallback) const noexcept;

    std::vector<State> states_;
    ComputedWindow pending_;
    std::vector<PointId> expandable_points_;
    std::vector<ItemCursor> item_cursors_;
    std::vector<int> item_tie_rank_by_id_;
    bool root_processed_ = false;
    long long generated_candidates_ = 0;
};

template <typename ShouldExpand>
SliceBuildResult CriticalSequence::process_slice(
    Weight ya, Weight yb, Weight compute_limit, const std::vector<Item>& items,
    ShouldExpand&& should_expand) {
    SliceBuildResult result;
    if (yb < ya || compute_limit < 0) return result;

    reserve_storage(compute_limit, items);

    if (!root_processed_ && ya == 0) {
        root_processed_ = true;
        if constexpr (stats_enabled_v<StatsMode::Basic>) {
            ++result.states_entered;
        }
        if (should_expand(0)) {
            if constexpr (stats_enabled_v<StatsMode::Basic>) {
                ++result.states_expanded;
            }
            expandable_points_.push_back(0);
            schedule_successors(0, compute_limit, items, result);
        }
    }

    // Scan the slice in weight order.  Newly accepted states can enqueue a
    // later position in this same slice, closing it transitively.
    const Weight first_weight = ya == std::numeric_limits<Weight>::max() ? ya : ya + 1;
    for (Weight weight = first_weight;; ++weight) {
        if (pending_.contains(weight)) {
            const Candidate candidate = pending_.take(weight);

            const Profit previous_profit = states_.back().profit;
            if (candidate.profit > previous_profit) {
                states_.push_back(
                    State{weight, candidate.profit, candidate.predecessor, candidate.item_id});
                const PointId id = states_.size() - 1;
                if constexpr (stats_enabled_v<StatsMode::Basic>) {
                    ++result.states_created;
                    ++result.states_entered;
                }
                if (should_expand(id)) {
                    if constexpr (stats_enabled_v<StatsMode::Basic>) {
                        ++result.states_expanded;
                    }
                    expandable_points_.push_back(id);
                    schedule_successors(id, compute_limit, items, result);
                }
            }
        }
        if (weight == yb) break;
    }
    return result;
}

template <typename ShouldIntroduce, typename ShouldExpand>
SliceBuildResult CriticalSequence::process_slice_incremental(
    Weight ya, Weight yb, Weight compute_limit,
    std::vector<ActiveItem>& active_items,
    const std::vector<ActiveItem>& items_by_weight, std::size_t& next_item,
    ShouldIntroduce&& should_introduce, ShouldExpand&& should_expand) {
    SliceBuildResult result;
    if (yb < ya || compute_limit < 0) return result;

    reserve_active_storage(compute_limit, active_items);
    // Cursor batches only contain targets in this slice.  Include its width in
    // the circular window so all live targets have distinct slots even when a
    // custom slice height exceeds every active item weight.
    pending_.configure(yb - ya);
    if (!root_processed_ && ya == 0) {
        root_processed_ = true;
        if constexpr (stats_enabled_v<StatsMode::Basic>) {
            ++result.states_entered;
        }
        if (should_expand(0)) {
            if constexpr (stats_enabled_v<StatsMode::Basic>) {
                ++result.states_expanded;
            }
            expandable_points_.push_back(0);
            if constexpr (stats_enabled_v<StatsMode::Full>) {
                sample_active_items(active_items, result);
            }
        }
    }

    const Weight target_limit = std::min(yb, compute_limit);
    // Resume each OCaml-style item cursor before visiting the slice.  Every
    // candidate needed in ]ya, yb] is therefore present before its target
    // weight is consumed.
    advance_active_cursors(active_items, target_limit, result);

    const Weight first_weight = ya == std::numeric_limits<Weight>::max() ? ya : ya + 1;
    for (Weight weight = first_weight;; ++weight) {
        Candidate selected{};
        bool has_selected = false;
        const Profit previous_profit = states_.back().profit;
        Profit envelope = previous_profit;

        if (pending_.contains(weight)) {
            selected = pending_.take(weight);
            has_selected = true;
            envelope = std::max(envelope, selected.profit);
        }

        while (next_item < items_by_weight.size() && items_by_weight[next_item].w == weight) {
            ActiveItem item = items_by_weight[next_item++];
            if constexpr (stats_enabled_v<StatsMode::Basic>) {
                ++result.items_considered_for_introduction;
            }
            if (item.p <= envelope) {
                if constexpr (stats_enabled_v<StatsMode::Basic>) {
                    ++result.items_rejected_by_envelope;
                }
                continue;
            }
            if (!should_introduce(item, envelope)) {
                if constexpr (stats_enabled_v<StatsMode::Basic>) {
                    ++result.items_rejected_by_bound;
                }
                continue;
            }

            initialize_item_cursor(item);
            advance_item_cursor(item, target_limit, result);
            const auto position = std::lower_bound(
                active_items.begin(), active_items.end(), item,
                [](const ActiveItem& existing, const ActiveItem& value) {
                    return existing.tie_rank < value.tie_rank;
                });
            active_items.insert(position, item);
            selected = Candidate{item.p, 0, item.id, item.tie_rank};
            has_selected = true;
            envelope = item.p;
            if constexpr (stats_enabled_v<StatsMode::Basic>) {
                ++result.items_introduced;
            }
            if constexpr (stats_enabled_v<StatsMode::Full>) {
                if (compute_limit > 0) {
                    const auto scaled = static_cast<__int128>(item.w) * 10;
                    const auto raw_decile = static_cast<std::size_t>(scaled / compute_limit);
                    const std::size_t decile = std::min<std::size_t>(9, raw_decile);
                    ++result.items_introduced_by_capacity_decile[decile];
                }
                if (!items_by_weight.empty() && items_by_weight.back().w > 0) {
                    const auto scaled = static_cast<__int128>(item.w) * 10;
                    const auto raw_decile = static_cast<std::size_t>(
                        scaled / items_by_weight.back().w);
                    const std::size_t decile = std::min<std::size_t>(9, raw_decile);
                    ++result.items_introduced_by_reduction_decile[decile];
                }
            }
        }

        if (has_selected && selected.profit > previous_profit) {
            states_.push_back(
                State{weight, selected.profit, selected.predecessor, selected.item_id});
            const PointId id = states_.size() - 1;
            if constexpr (stats_enabled_v<StatsMode::Basic>) {
                ++result.states_created;
                ++result.states_entered;
            }
            if (should_expand(id)) {
                if constexpr (stats_enabled_v<StatsMode::Basic>) {
                    ++result.states_expanded;
                }
                expandable_points_.push_back(id);
                schedule_current_active_successors(
                    id, target_limit, active_items, result);
            }
        }
        if (weight == yb) break;
    }
    synchronize_active_cursors(active_items, target_limit);
    return result;
}

}  // namespace ukp::faithful::detail
