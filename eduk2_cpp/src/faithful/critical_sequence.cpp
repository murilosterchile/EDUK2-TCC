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
    if (!slot.occupied || slot.weight != weight || candidate.profit > slot.candidate.profit ||
        (candidate.profit == slot.candidate.profit &&
         candidate.tie_rank < slot.candidate.tie_rank)) {
        // Equal-profit candidates retain the highest ratio-order priority.
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
           pending_.estimated_bytes() + expandable_points_.capacity() * sizeof(PointId) +
           item_tie_rank_by_id_.capacity() * sizeof(int);
}

void CriticalSequence::configure_item_order(const std::vector<Item>& ratio_ordered_items) {
    int maximum_id = -1;
    for (const Item& item : ratio_ordered_items) maximum_id = std::max(maximum_id, item.id);
    if (maximum_id < 0 ||
        static_cast<std::size_t>(maximum_id) > ratio_ordered_items.size() * 8 + 1024) {
        item_tie_rank_by_id_.clear();
        return;
    }
    item_tie_rank_by_id_.assign(static_cast<std::size_t>(maximum_id) + 1, -1);
    for (std::size_t rank = 0; rank < ratio_ordered_items.size(); ++rank) {
        const int id = ratio_ordered_items[rank].id;
        if (id >= 0 && item_tie_rank_by_id_[static_cast<std::size_t>(id)] < 0) {
            item_tie_rank_by_id_[static_cast<std::size_t>(id)] = static_cast<int>(rank);
        }
    }
}

int CriticalSequence::tie_rank(const Item& item, std::size_t fallback) const noexcept {
    if (item.id >= 0 && static_cast<std::size_t>(item.id) < item_tie_rank_by_id_.size()) {
        const int rank = item_tie_rank_by_id_[static_cast<std::size_t>(item.id)];
        if (rank >= 0) return rank;
    }
    return fallback > static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max() : static_cast<int>(fallback);
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
    for (std::size_t index = 0; index < items.size(); ++index) {
        const Item& item = items[index];
        ++result.successor_item_scans;
        const Weight weight = safe_add(base.weight, item.w);
        if (weight > compute_limit) continue;
        ++generated_candidates_;
        ++result.successor_attempts;
        ++result.points_generated;
        const Candidate candidate{safe_add(base.profit, item.p), parent, item.id,
                                  tie_rank(item, index)};
        pending_.store(weight, candidate);
    }
}

void CriticalSequence::backfill_item(const Item& item, Weight compute_limit,
                                     SliceBuildResult& result) {
    pending_.configure(item.w);
    const int rank = tie_rank(item, 0);
    // The direct root + item transition is installed at the introduction
    // weight itself.  Backfill starts at the first positive state, matching
    // Init.introduce's next_built_upon = (0,1).
    for (const PointId parent : expandable_points_) {
        if (parent == 0) continue;
        ++result.successor_item_scans;
        const State& base = state(parent);
        const Weight weight = safe_add(base.weight, item.w);
        if (weight > compute_limit) continue;
        ++generated_candidates_;
        ++result.successor_attempts;
        ++result.backfill_attempts;
        ++result.points_generated;
        pending_.store(weight, Candidate{safe_add(base.profit, item.p), parent, item.id, rank});
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
        if (should_expand(0)) {
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

            const Profit previous_profit = state(skip_points_.back()).profit;
            if (candidate.profit > previous_profit) {
                states_.push_back(State{weight, candidate.profit, candidate.predecessor, candidate.item_id});
                const PointId id = states_.size() - 1;
                skip_points_.push_back(id);
                ++result.states_created;
                ++result.states_entered;
                if (should_expand(id)) {
                    expandable_points_.push_back(id);
                    schedule_successors(id, compute_limit, items, result);
                }
            }
        }
        if (weight == yb) break;
    }
    return result;
}

SliceBuildResult CriticalSequence::process_slice_incremental(
    Weight ya, Weight yb, Weight compute_limit, std::vector<Item>& active_items,
    const std::vector<Item>& items_by_weight, std::size_t& next_item,
    const std::function<bool(const Item&, Profit)>& should_introduce,
    const std::function<bool(PointId)>& should_expand) {
    SliceBuildResult result;
    if (yb < ya || compute_limit < 0) return result;

    reserve_storage(compute_limit, active_items);
    if (!root_processed_ && ya == 0) {
        root_processed_ = true;
        ++result.states_entered;
        if (should_expand(0)) {
            expandable_points_.push_back(0);
            schedule_successors(0, compute_limit, active_items, result);
        }
    }

    const Weight first_weight = ya == std::numeric_limits<Weight>::max() ? ya : ya + 1;
    for (Weight weight = first_weight;; ++weight) {
        Candidate selected{};
        bool has_selected = false;
        const Profit previous_profit = state(skip_points_.back()).profit;
        Profit envelope = previous_profit;

        if (pending_.contains(weight)) {
            selected = pending_.take(weight);
            has_selected = true;
            envelope = std::max(envelope, selected.profit);
        }

        while (next_item < items_by_weight.size() && items_by_weight[next_item].w == weight) {
            const Item& item = items_by_weight[next_item++];
            ++result.items_considered_for_introduction;
            if (item.p <= envelope) {
                ++result.items_rejected_by_envelope;
                continue;
            }
            if (!should_introduce(item, envelope)) {
                ++result.items_rejected_by_bound;
                continue;
            }

            backfill_item(item, compute_limit, result);
            const auto position = std::lower_bound(
                active_items.begin(), active_items.end(), item,
                [](const Item& existing, const Item& value) { return better_ratio(existing, value); });
            active_items.insert(position, item);
            selected = Candidate{item.p, 0, item.id, tie_rank(item, 0)};
            has_selected = true;
            envelope = item.p;
            ++result.items_introduced;
        }

        if (has_selected && selected.profit > previous_profit) {
            states_.push_back(State{weight, selected.profit, selected.predecessor, selected.item_id});
            const PointId id = states_.size() - 1;
            skip_points_.push_back(id);
            ++result.states_created;
            ++result.states_entered;
            if (should_expand(id)) {
                expandable_points_.push_back(id);
                schedule_successors(id, compute_limit, active_items, result);
            }
        }
        if (weight == yb) break;
    }
    return result;
}

}  // namespace ukp::faithful::detail
