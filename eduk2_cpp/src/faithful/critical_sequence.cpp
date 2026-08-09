#include "critical_sequence.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ukp::faithful::detail {

CriticalSequence::CriticalSequence() {
    states_.push_back(State{0, 0, no_point, -1});
    skip_points_.push_back(0);
}

std::size_t CriticalSequence::ComputedWindow::index(Weight weight) const {
    return static_cast<std::size_t>(weight % static_cast<Weight>(slots_.size()));
}

void CriticalSequence::ComputedWindow::configure(Weight largest_item_weight) {
    if (!slots_.empty() && largest_item_weight <= largest_item_weight_) return;
    if (largest_item_weight == std::numeric_limits<Weight>::max()) {
        throw std::length_error("computed-window item weight is too large");
    }
    const auto size = static_cast<std::size_t>(largest_item_weight + 1);
    if (static_cast<Weight>(size) != largest_item_weight + 1) {
        throw std::length_error("computed-window does not fit in size_t");
    }

    std::vector<Slot> replacement(size);
    for (const Slot& slot : slots_) {
        if (!slot.occupied) continue;
        Slot& destination = replacement[static_cast<std::size_t>(
            slot.weight % static_cast<Weight>(replacement.size()))];
        destination = slot;
    }
    slots_ = std::move(replacement);
    largest_item_weight_ = largest_item_weight;
}

void CriticalSequence::ComputedWindow::store(Weight weight, const Candidate& candidate) {
    Slot& slot = slots_[index(weight)];
    if (!slot.occupied || slot.weight != weight || candidate.profit > slot.candidate.profit) {
        // Equal-profit candidates deliberately retain the first generated one.
        slot = Slot{candidate, weight, true};
    }
}

bool CriticalSequence::ComputedWindow::contains(Weight weight) const {
    if (slots_.empty()) return false;
    const Slot& slot = slots_[index(weight)];
    return slot.occupied && slot.weight == weight;
}

CriticalSequence::Candidate CriticalSequence::ComputedWindow::take(Weight weight) {
    Slot& slot = slots_[index(weight)];
    if (!slot.occupied || slot.weight != weight) {
        throw std::logic_error("computed-window candidate is missing");
    }
    const Candidate candidate = slot.candidate;
    slot.occupied = false;
    return candidate;
}

std::size_t CriticalSequence::ComputedWindow::estimated_bytes() const noexcept {
    return slots_.capacity() * sizeof(Slot);
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
           pending_.estimated_bytes();
}

void CriticalSequence::reserve_storage(Weight compute_limit, const std::vector<Item>& items) {
    if (items.empty()) return;
    Weight smallest_weight = items.front().w;
    Weight largest_weight = items.front().w;
    for (const Item& item : items) {
        smallest_weight = std::min(smallest_weight, item.w);
        largest_weight = std::max(largest_weight, item.w);
    }
    pending_.configure(largest_weight);

    if (smallest_weight <= 0 || compute_limit < 0) return;
    const Weight chain_estimate = compute_limit / smallest_weight + 1;
    constexpr std::size_t kMaximumInitialReserve = 1U << 20;
    const std::size_t estimate = static_cast<std::size_t>(std::min<Weight>(
        chain_estimate, static_cast<Weight>(kMaximumInitialReserve)));
    states_.reserve(std::max(states_.size(), estimate));
    skip_points_.reserve(std::max(skip_points_.size(), estimate));
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
        pending_.store(weight, candidate);
    }
}

SliceBuildResult CriticalSequence::process_slice(
    Weight ya, Weight yb, Weight compute_limit, const std::vector<Item>& items,
    const std::function<bool(PointId)>& should_expand) {
    SliceBuildResult result;
    if (yb < ya || compute_limit < 0) return result;

    reserve_storage(compute_limit, items);

    if (!root_processed_ && ya == 0) {
        root_processed_ = true;
        ++result.states_entered;
        if (should_expand(0)) schedule_successors(0, compute_limit, items, result);
    }

    // Scan the slice in weight order.  Newly accepted states can enqueue a
    // later position in this same slice, closing it transitively.
    const Weight first_weight = ya == std::numeric_limits<Weight>::max() ? ya : ya + 1;
    for (Weight weight = first_weight;; ++weight) {
        if (pending_.contains(weight)) {
            const Candidate candidate = pending_.take(weight);

            const Profit previous_profit = state(skip_points_.back()).profit;
            if (candidate.profit > previous_profit) {
                states_.push_back(State{weight, candidate.profit, candidate.predecessor, candidate.item_id});
                const PointId id = states_.size() - 1;
                skip_points_.push_back(id);
                ++result.states_created;
                ++result.states_entered;
                if (should_expand(id)) schedule_successors(id, compute_limit, items, result);
            }
        }
        if (weight == yb) break;
    }
    return result;
}

}  // namespace ukp::faithful::detail
