#pragma once

#include "ukp/types.hpp"

#include <cstddef>
#include <functional>
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

struct SliceBuildResult {
    long long states_entered = 0;
    long long successor_attempts = 0;
    long long successor_item_scans = 0;
    long long backfill_attempts = 0;
    long long states_created = 0;
    long long points_generated = 0;
    long long items_considered_for_introduction = 0;
    long long items_introduced = 0;
    long long items_rejected_by_envelope = 0;
    long long items_rejected_by_bound = 0;
};

class CriticalSequence {
public:
    CriticalSequence();

    // Processes ]ya, yb].  The visitor decides whether an accepted state may
    // expand; this leaves bounds and other solver policy outside the DP data
    // structure.  Returning false preserves the state but emits no successors.
    SliceBuildResult process_slice(Weight ya, Weight yb, Weight compute_limit,
                                   const std::vector<Item>& items,
                                   const std::function<bool(PointId)>& should_expand);

    // PYAsUKP's Select.next_lightest + Init.introduce counterpart.  Items are
    // supplied in nondecreasing weight order.  At weight wi, an item is made
    // active only when pi strictly raises the envelope built without it and
    // the contextual predicate accepts it.  Its transitions from earlier
    // expandable states are then backfilled before the scan advances.
    SliceBuildResult process_slice_incremental(
        Weight ya, Weight yb, Weight compute_limit,
        std::vector<Item>& active_items,
        const std::vector<Item>& items_by_weight,
        std::size_t& next_item,
        const std::function<bool(const Item&, Profit)>& should_introduce,
        const std::function<bool(PointId)>& should_expand);

    // Establishes PYAsUKP's ratio-order tie priority independently of the
    // order in which weight-selected items become active.
    void configure_item_order(const std::vector<Item>& ratio_ordered_items);

    [[nodiscard]] PointId state_at_or_before(Weight y) const;
    [[nodiscard]] Profit value_at(Weight y) const;
    [[nodiscard]] const State& state(PointId id) const;
    [[nodiscard]] const std::vector<PointId>& skip_points() const noexcept;
    [[nodiscard]] std::size_t stored_states() const noexcept;
    [[nodiscard]] long long generated_candidates() const noexcept;
    [[nodiscard]] std::size_t estimated_bytes() const noexcept;

private:
    struct Candidate {
        Profit profit;
        PointId predecessor;
        int item_id;
        int tie_rank;
    };

    // All successors of the weight currently being visited are at most the
    // largest item weight ahead.  A circular window of that width therefore
    // retains every pending candidate without a capacity-sized table.
    class ComputedWindow {
    public:
        void configure(Weight largest_item_weight);
        void store(Weight weight, const Candidate& candidate);
        [[nodiscard]] bool contains(Weight weight) const;
        [[nodiscard]] Candidate take(Weight weight);
        [[nodiscard]] std::size_t estimated_bytes() const noexcept;

    private:
        struct Slot {
            Candidate candidate{};
            Weight weight = -1;
            bool occupied = false;
        };

        [[nodiscard]] std::size_t index(Weight weight) const;

        std::vector<Slot> slots_;
        Weight largest_item_weight_ = 0;
    };

    void schedule_successors(PointId parent, Weight compute_limit,
                             const std::vector<Item>& items, SliceBuildResult& result);
    void backfill_item(const Item& item, Weight compute_limit, SliceBuildResult& result);
    void reserve_storage(Weight compute_limit, const std::vector<Item>& items);
    [[nodiscard]] int tie_rank(const Item& item, std::size_t fallback) const noexcept;

    std::vector<State> states_;
    std::vector<PointId> skip_points_;
    ComputedWindow pending_;
    std::vector<PointId> expandable_points_;
    std::vector<int> item_tie_rank_by_id_;
    bool root_processed_ = false;
    long long generated_candidates_ = 0;
};

}  // namespace ukp::faithful::detail
