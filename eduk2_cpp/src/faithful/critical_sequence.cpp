#include "critical_sequence.hpp"

#include <algorithm>

namespace ukp::faithful::detail {

CriticalSequence::CriticalSequence() {
    states_.push_back(State{0, 0, no_point, -1});
    skip_points_.push_back(0);
}

const State& CriticalSequence::state(PointId id) const { return states_.at(id); }

PointId CriticalSequence::state_at_or_before(Weight y) const {
    if (y < 0) return skip_points_.front();
    const auto position = std::upper_bound(skip_points_.begin(), skip_points_.end(), y,
        [&](Weight value, PointId id) { return value < states_[id].weight; });
    return position == skip_points_.begin() ? skip_points_.front() : *std::prev(position);
}

Profit CriticalSequence::value_at(Weight y) const {
    return state(state_at_or_before(y)).profit;
}

const std::vector<PointId>& CriticalSequence::skip_points() const noexcept { return skip_points_; }
std::size_t CriticalSequence::stored_states() const noexcept { return states_.size(); }
long long CriticalSequence::generated_candidates() const noexcept { return generated_candidates_; }
std::size_t CriticalSequence::estimated_bytes() const noexcept {
    return states_.size() * sizeof(State) + skip_points_.size() * sizeof(PointId) +
           pending_.size() * (sizeof(Weight) + sizeof(Candidate));
}

void CriticalSequence::schedule_successors(PointId parent, Weight compute_limit,
                                           const std::vector<Item>& items,
                                           SliceBuildResult& result) {
    const State& base = state(parent);
    for (const Item& item : items) {
        const Weight weight = safe_add(base.weight, item.w);
        if (weight > compute_limit) continue;
        ++generated_candidates_;
        ++result.successor_attempts;
        ++result.points_generated;
        const Candidate candidate{safe_add(base.profit, item.p), parent, item.id};
        const auto found = pending_.find(weight);
        // Equal-profit candidates deliberately retain the first generated one.
        if (found == pending_.end() || candidate.profit > found->second.profit) {
            pending_[weight] = candidate;
        }
    }
}

SliceBuildResult CriticalSequence::process_slice(
    Weight ya, Weight yb, Weight compute_limit, const std::vector<Item>& items,
    const std::function<bool(PointId)>& should_expand) {
    SliceBuildResult result;
    if (yb < ya || compute_limit < 0) return result;

    if (!root_processed_ && ya == 0) {
        root_processed_ = true;
        ++result.states_entered;
        if (should_expand(0)) schedule_successors(0, compute_limit, items, result);
    }

    // Each iteration removes the least pending weight.  Newly accepted states
    // can enqueue more candidates in this same slice, closing it transitively.
    while (!pending_.empty() && pending_.begin()->first <= yb) {
        auto current = pending_.begin();
        const Weight weight = current->first;
        const Candidate candidate = current->second;
        pending_.erase(current);
        if (weight <= ya || weight > compute_limit) continue;

        const Profit previous_profit = state(skip_points_.back()).profit;
        if (candidate.profit <= previous_profit) continue;

        states_.push_back(State{weight, candidate.profit, candidate.predecessor, candidate.item_id});
        const PointId id = states_.size() - 1;
        skip_points_.push_back(id);
        ++result.states_created;
        ++result.states_entered;
        if (should_expand(id)) schedule_successors(id, compute_limit, items, result);
    }
    return result;
}

}  // namespace ukp::faithful::detail
