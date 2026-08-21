#include "critical_sequence.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ukp::faithful::detail {

CriticalSequence::CriticalSequence() {
    states_.push_back(State{0, 0, no_point, -1});
}

std::size_t CriticalSequence::ComputedWindow::index(Weight weight) const {
    return static_cast<std::size_t>(weight) & index_mask_;
}

void CriticalSequence::ComputedWindow::configure(Weight largest_item_weight) {
    if (!slots_.empty() && largest_item_weight <= largest_item_weight_) return;
    if (largest_item_weight < 0) {
        throw std::invalid_argument("negative computed-window item weight");
    }
    if (largest_item_weight == std::numeric_limits<Weight>::max()) {
        throw std::length_error("computed-window item weight is too large");
    }
    const auto required_size = static_cast<std::size_t>(largest_item_weight + 1);
    if (static_cast<Weight>(required_size) != largest_item_weight + 1) {
        throw std::length_error("computed-window does not fit in size_t");
    }

    std::size_t size = 1;
    while (size < required_size) {
        if (size > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::length_error("computed-window power-of-two size overflow");
        }
        size <<= 1;
    }
    largest_item_weight_ = largest_item_weight;
    if (size == slots_.size()) return;

    std::vector<Slot> replacement(size);
    const std::size_t replacement_mask = size - 1;
    for (const Slot& slot : slots_) {
        if (slot.weight < 0) continue;
        Slot& destination = replacement[static_cast<std::size_t>(slot.weight) & replacement_mask];
        destination = slot;
    }
    slots_ = std::move(replacement);
    index_mask_ = replacement_mask;
}

void CriticalSequence::ComputedWindow::store(Weight weight, const Candidate& candidate) {
    Slot& slot = slots_[index(weight)];
    if (slot.weight != weight) {
        if (slot.weight >= 0) ++index_collisions_;
        ++candidates_stored_;
        slot = Slot{candidate, weight};
        return;
    }

    ++collisions_;
    if (candidate.profit > slot.candidate.profit ||
        (candidate.profit == slot.candidate.profit &&
         candidate.tie_rank < slot.candidate.tie_rank)) {
        // Equal-profit candidates retain the highest ratio-order priority.
        ++candidates_stored_;
        ++replacements_;
        slot = Slot{candidate, weight};
    } else {
        ++rejections_;
    }
}

bool CriticalSequence::ComputedWindow::contains(Weight weight) const {
    if (slots_.empty()) return false;
    const Slot& slot = slots_[index(weight)];
    return slot.weight == weight;
}

CriticalSequence::Candidate CriticalSequence::ComputedWindow::take(Weight weight) {
    Slot& slot = slots_[index(weight)];
    if (slot.weight != weight) {
        throw std::logic_error("computed-window candidate is missing");
    }
    const Candidate candidate = slot.candidate;
    slot.weight = -1;
    return candidate;
}

std::size_t CriticalSequence::ComputedWindow::estimated_bytes() const noexcept {
    return slots_.capacity() * sizeof(Slot);
}

long long CriticalSequence::ComputedWindow::candidates_stored() const noexcept {
    return candidates_stored_;
}
long long CriticalSequence::ComputedWindow::collisions() const noexcept {
    return collisions_;
}
long long CriticalSequence::ComputedWindow::replacements() const noexcept {
    return replacements_;
}
long long CriticalSequence::ComputedWindow::rejections() const noexcept {
    return rejections_;
}
long long CriticalSequence::ComputedWindow::index_collisions() const noexcept {
    return index_collisions_;
}

const State& CriticalSequence::state(PointId id) const { return states_.at(id); }

PointId CriticalSequence::state_at_or_before(Weight y) const {
    if (y < 0) return 0;
    const auto position = std::upper_bound(
        states_.begin(), states_.end(), y,
        [](Weight value, const State& state) { return value < state.weight; });
    return position == states_.begin()
        ? PointId{0}
        : static_cast<PointId>(std::prev(position) - states_.begin());
}

Profit CriticalSequence::value_at(Weight y) const {
    return state(state_at_or_before(y)).profit;
}

const std::vector<State>& CriticalSequence::states() const noexcept { return states_; }
std::size_t CriticalSequence::stored_states() const noexcept { return states_.size(); }
long long CriticalSequence::generated_candidates() const noexcept { return generated_candidates_; }
long long CriticalSequence::candidates_stored() const noexcept {
    return pending_.candidates_stored();
}
long long CriticalSequence::computed_window_collisions() const noexcept {
    return pending_.collisions();
}
long long CriticalSequence::computed_window_replacements() const noexcept {
    return pending_.replacements();
}
long long CriticalSequence::computed_window_rejections() const noexcept {
    return pending_.rejections();
}
long long CriticalSequence::computed_window_index_collisions() const noexcept {
    return pending_.index_collisions();
}
std::size_t CriticalSequence::estimated_bytes() const noexcept {
    return states_.size() * sizeof(State) + pending_.estimated_bytes() +
           expandable_points_.capacity() * sizeof(PointId) +
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
    if (compute_limit >= 0) {
        constexpr std::size_t kMaximumInitialReserve = 1U << 18;
        const std::size_t estimate = compute_limit >= static_cast<Weight>(kMaximumInitialReserve - 1)
            ? kMaximumInitialReserve : static_cast<std::size_t>(compute_limit + 1);
        states_.reserve(estimate);
        expandable_points_.reserve(estimate);
    }

    if (items.empty()) return;
    Weight largest_weight = items.front().w;
    for (const Item& item : items) {
        largest_weight = std::max(largest_weight, item.w);
    }
    pending_.configure(largest_weight);
}

void CriticalSequence::reserve_active_storage(
    Weight compute_limit, const std::vector<ActiveItem>& items) {
    if (compute_limit >= 0) {
        constexpr std::size_t kMaximumInitialReserve = 1U << 18;
        const std::size_t estimate = compute_limit >= static_cast<Weight>(kMaximumInitialReserve - 1)
            ? kMaximumInitialReserve : static_cast<std::size_t>(compute_limit + 1);
        states_.reserve(estimate);
        expandable_points_.reserve(estimate);
    }

    if (items.empty()) return;
    Weight largest_weight = items.front().w;
    for (const ActiveItem& item : items) {
        largest_weight = std::max(largest_weight, item.w);
    }
    pending_.configure(largest_weight);
}

void CriticalSequence::schedule_successors(PointId parent, Weight compute_limit,
                                           const std::vector<Item>& items,
                                           SliceBuildResult& result) {
    const State& base = state(parent);
    const Weight remaining_capacity = compute_limit - base.weight;
    long long generated = 0;
    ++result.active_item_samples;
    result.active_items_sum += static_cast<long long>(items.size());
    result.active_items_max = std::max(
        result.active_items_max, static_cast<long long>(items.size()));
    result.successor_item_scans += static_cast<long long>(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        const Item& item = items[index];
        if (item.w > remaining_capacity) continue;
        const Weight weight = base.weight + item.w;
        ++generated;
        const Candidate candidate{safe_add(base.profit, item.p), parent, item.id,
                                  tie_rank(item, index)};
        pending_.store(weight, candidate);
    }
    generated_candidates_ += generated;
    result.successor_attempts += generated;
    result.points_generated += generated;
}

void CriticalSequence::schedule_active_successors(
    PointId parent, Weight compute_limit, const std::vector<ActiveItem>& items,
    SliceBuildResult& result) {
    const State& base = state(parent);
    const Weight remaining_capacity = compute_limit - base.weight;
    long long generated = 0;
    ++result.active_item_samples;
    result.active_items_sum += static_cast<long long>(items.size());
    result.active_items_max = std::max(
        result.active_items_max, static_cast<long long>(items.size()));
    result.successor_item_scans += static_cast<long long>(items.size());
    for (const ActiveItem& item : items) {
        if (item.w > remaining_capacity) continue;
        const Weight weight = base.weight + item.w;
        ++generated;
        pending_.store(weight, Candidate{
            safe_add(base.profit, item.p), parent, item.id, item.tie_rank});
    }
    generated_candidates_ += generated;
    result.successor_attempts += generated;
    result.points_generated += generated;
}

void CriticalSequence::backfill_item(const ActiveItem& item, Weight compute_limit,
                                     SliceBuildResult& result) {
    pending_.configure(item.w);
    const int rank = item.tie_rank;
    // The direct root + item transition is installed at the introduction
    // weight itself.  Backfill starts at the first positive state, matching
    // Init.introduce's next_built_upon = (0,1).
    for (const PointId parent : expandable_points_) {
        if (parent == 0) continue;
        const State& base = state(parent);
        ++result.successor_item_scans;
        if (item.w > compute_limit - base.weight) continue;
        const Weight weight = base.weight + item.w;
        ++generated_candidates_;
        ++result.successor_attempts;
        ++result.backfill_attempts;
        ++result.points_generated;
        pending_.store(weight, Candidate{safe_add(base.profit, item.p), parent, item.id, rank});
    }
}

}  // namespace ukp::faithful::detail
