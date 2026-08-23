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

void CriticalSequence::ComputedWindow::configure(Weight live_span) {
    if (!slots_.empty() && live_span <= configured_span_) return;
    if (live_span < 0) {
        throw std::invalid_argument("negative computed-window live span");
    }
    if (live_span == std::numeric_limits<Weight>::max()) {
        throw std::length_error("computed-window live span is too large");
    }
    const auto required_size = static_cast<std::size_t>(live_span + 1);
    if (static_cast<Weight>(required_size) != live_span + 1) {
        throw std::length_error("computed-window does not fit in size_t");
    }

    std::size_t size = 1;
    while (size < required_size) {
        if (size > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::length_error("computed-window power-of-two size overflow");
        }
        size <<= 1;
    }
    configured_span_ = live_span;
#ifndef NDEBUG
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        legacy_configure(live_span);
    }
#endif
    if (size == slots_.size()) return;

    std::vector<Slot> replacement(size);
    const std::size_t replacement_mask = size - 1;
    for (const Slot& slot : slots_) {
        if (slot.weight < 0) continue;
        Slot& destination =
            replacement[static_cast<std::size_t>(slot.weight) & replacement_mask];
#ifndef NDEBUG
        assert(destination.weight < 0);
#endif
        destination = slot;
    }
    slots_ = std::move(replacement);
    index_mask_ = replacement_mask;
#ifndef NDEBUG
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        assert_all_winners_match_legacy();
    }
#endif
}

void CriticalSequence::ComputedWindow::store(
    Weight weight, Profit profit, PointId predecessor, int item_id, int tie_rank) {
#ifndef NDEBUG
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        legacy_store(weight, Candidate{profit, tie_rank, item_id, predecessor});
    }
#endif

    Slot& slot = slots_[index(weight)];
    if (slot.weight != weight) {
        if constexpr (stats_enabled_v<StatsMode::Full>) {
            if (slot.weight >= 0) ++index_collisions_;
            ++candidates_stored_;
        }
        slot.weight = weight;
        slot.candidate.profit = profit;
        slot.candidate.tie_rank = tie_rank;
        slot.candidate.item_id = item_id;
        slot.candidate.predecessor = predecessor;
#ifndef NDEBUG
        if constexpr (stats_enabled_v<StatsMode::Full>) {
            assert_winner_matches_legacy(weight);
        }
#endif
        return;
    }

    if constexpr (stats_enabled_v<StatsMode::Full>) {
        ++collisions_;
    }

    const Profit current_profit = slot.candidate.profit;
    const bool wins = profit > current_profit ||
        (profit == current_profit && tie_rank < slot.candidate.tie_rank);
    if (!wins) {
        if constexpr (stats_enabled_v<StatsMode::Full>) {
            ++rejections_;
        }
#ifndef NDEBUG
        if constexpr (stats_enabled_v<StatsMode::Full>) {
            assert_winner_matches_legacy(weight);
        }
#endif
        return;
    }

    // Equal-profit candidates retain the highest ratio-order priority.
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        ++candidates_stored_;
        ++replacements_;
    }
    slot.candidate.profit = profit;
    slot.candidate.tie_rank = tie_rank;
    slot.candidate.item_id = item_id;
    slot.candidate.predecessor = predecessor;
#ifndef NDEBUG
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        assert_winner_matches_legacy(weight);
    }
#endif
}

bool CriticalSequence::ComputedWindow::contains(Weight weight) const {
    if (slots_.empty()) return false;
    const bool present = slots_[index(weight)].weight == weight;
#ifndef NDEBUG
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        const bool legacy_present = !legacy_slots_.empty() &&
            legacy_slots_[static_cast<std::size_t>(weight) & legacy_index_mask_].weight == weight;
        assert(present == legacy_present);
    }
#endif
    return present;
}

CriticalSequence::Candidate CriticalSequence::ComputedWindow::take(Weight weight) {
    Slot& slot = slots_[index(weight)];
    if (slot.weight != weight) {
        throw std::logic_error("computed-window candidate is missing");
    }
    const Candidate candidate = slot.candidate;
#ifndef NDEBUG
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        const Candidate legacy = legacy_take(weight);
        assert(candidate.profit == legacy.profit);
        assert(candidate.predecessor == legacy.predecessor);
        assert(candidate.item_id == legacy.item_id);
        assert(candidate.tie_rank == legacy.tie_rank);
    }
#endif
    slot.weight = -1;
    return candidate;
}

std::size_t CriticalSequence::ComputedWindow::estimated_bytes() const noexcept {
    std::size_t bytes = slots_.capacity() * sizeof(Slot);
#ifndef NDEBUG
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        bytes += legacy_slots_.capacity() * sizeof(LegacySlot);
    }
#endif
    return bytes;
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

#ifndef NDEBUG
void CriticalSequence::ComputedWindow::debug_legacy_configure(Weight live_span) {
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        legacy_configure(live_span);
    } else {
        (void)live_span;
    }
}

void CriticalSequence::ComputedWindow::legacy_configure(Weight live_span) {
    if (!legacy_slots_.empty() && live_span <= legacy_configured_span_) return;
    if (live_span < 0) {
        throw std::invalid_argument("negative computed-window item weight");
    }
    if (live_span == std::numeric_limits<Weight>::max()) {
        throw std::length_error("computed-window item weight is too large");
    }
    const auto required_size = static_cast<std::size_t>(live_span + 1);
    if (static_cast<Weight>(required_size) != live_span + 1) {
        throw std::length_error("computed-window does not fit in size_t");
    }

    std::size_t size = 1;
    while (size < required_size) {
        if (size > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::length_error("computed-window power-of-two size overflow");
        }
        size <<= 1;
    }
    legacy_configured_span_ = live_span;
    if (size == legacy_slots_.size()) return;

    std::vector<LegacySlot> replacement(size);
    const std::size_t replacement_mask = size - 1;
    for (const LegacySlot& slot : legacy_slots_) {
        if (slot.weight < 0) continue;
        LegacySlot& destination =
            replacement[static_cast<std::size_t>(slot.weight) & replacement_mask];
        destination = slot;
    }
    legacy_slots_ = std::move(replacement);
    legacy_index_mask_ = replacement_mask;
}

void CriticalSequence::ComputedWindow::legacy_store(
    Weight weight, const Candidate& candidate) {
    LegacySlot& slot = legacy_slots_[
        static_cast<std::size_t>(weight) & legacy_index_mask_];
    const LegacyCandidate legacy_candidate{
        candidate.profit, candidate.predecessor, candidate.item_id, candidate.tie_rank};
    if (slot.weight != weight) {
        slot = LegacySlot{legacy_candidate, weight};
        return;
    }
    if (legacy_candidate.profit > slot.candidate.profit ||
        (legacy_candidate.profit == slot.candidate.profit &&
         legacy_candidate.tie_rank < slot.candidate.tie_rank)) {
        slot = LegacySlot{legacy_candidate, weight};
    }
}

CriticalSequence::Candidate CriticalSequence::ComputedWindow::legacy_take(Weight weight) {
    LegacySlot& slot = legacy_slots_[
        static_cast<std::size_t>(weight) & legacy_index_mask_];
    assert(slot.weight == weight);
    const Candidate candidate{slot.candidate.profit, slot.candidate.tie_rank,
                              slot.candidate.item_id, slot.candidate.predecessor};
    slot.weight = -1;
    return candidate;
}

void CriticalSequence::ComputedWindow::assert_winner_matches_legacy(Weight weight) const {
    const Slot& slot = slots_[index(weight)];
    const LegacySlot& legacy = legacy_slots_[
        static_cast<std::size_t>(weight) & legacy_index_mask_];
    assert(slot.weight == weight);
    assert(legacy.weight == weight);
    assert(slot.candidate.profit == legacy.candidate.profit);
    assert(slot.candidate.predecessor == legacy.candidate.predecessor);
    assert(slot.candidate.item_id == legacy.candidate.item_id);
    assert(slot.candidate.tie_rank == legacy.candidate.tie_rank);
}

void CriticalSequence::ComputedWindow::assert_all_winners_match_legacy() const {
    for (const Slot& slot : slots_) {
        if (slot.weight < 0) continue;
        assert_winner_matches_legacy(slot.weight);
    }
    for (const LegacySlot& legacy : legacy_slots_) {
        if (legacy.weight < 0) continue;
        assert(slots_[index(legacy.weight)].weight == legacy.weight);
        assert_winner_matches_legacy(legacy.weight);
    }
}
#endif

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
           item_cursors_.capacity() * sizeof(ItemCursor) +
           item_tie_rank_by_id_.capacity() * sizeof(int) +
           active_items_by_weight_.capacity() * sizeof(ActiveItem) +
           active_item_alive_by_rank_.capacity() * sizeof(unsigned char);
}

std::size_t CriticalSequence::unprocessed_historical_states(
    const ActiveItem& item, Weight target_limit) const {
    const ItemCursor& cursor_state = cursor_for(item);
    if (target_limit < item.w) return 0;
    const std::size_t represented_end = std::min(cursor_state.built_upon_end,
                                                 expandable_points_.size());
    const Weight maximum_parent_weight = target_limit - item.w;
    const auto eligible_end_position = std::upper_bound(
        expandable_points_.begin(),
        expandable_points_.begin() + static_cast<std::ptrdiff_t>(represented_end),
        maximum_parent_weight,
        [this](Weight maximum, PointId point) {
            return maximum < states_[point].weight;
        });
    const std::size_t eligible_end = static_cast<std::size_t>(
        eligible_end_position - expandable_points_.begin());
    const std::size_t cursor = std::min(cursor_state.next_built_upon, eligible_end);
    return eligible_end - cursor;
}

long long CriticalSequence::item_backfill_attempts(
    const ActiveItem& item) const {
    return cursor_for(item).backfill_attempts;
}

bool CriticalSequence::item_was_introduced(const ActiveItem& item) const {
    return cursor_for(item).introduced;
}

void CriticalSequence::stop_item_after_slice(const ActiveItem& item) {
    ItemCursor& cursor_state = cursor_for(item);
    cursor_state.built_upon_end = cursor_state.next_built_upon;

    if (item.tie_rank < 0 ||
        static_cast<std::size_t>(item.tie_rank) >= active_item_alive_by_rank_.size()) {
        throw std::logic_error("stopped item tie rank is outside active weight view");
    }
    unsigned char& alive = active_item_alive_by_rank_[
        static_cast<std::size_t>(item.tie_rank)];
    if (alive == 0) {
        throw std::logic_error("stopped item is absent from active weight view");
    }
    alive = 0;
    active_items_by_weight_dirty_ = true;
}

bool CriticalSequence::active_item_weight_less(
    const ActiveItem& left, const ActiveItem& right) noexcept {
    if (left.w != right.w) return left.w < right.w;
    return left.tie_rank < right.tie_rank;
}

void CriticalSequence::add_active_item_by_weight(const ActiveItem& item) {
    if (item.tie_rank < 0 ||
        static_cast<std::size_t>(item.tie_rank) >= active_item_alive_by_rank_.size()) {
        throw std::logic_error("introduced item tie rank is outside active weight view");
    }
    unsigned char& alive = active_item_alive_by_rank_[
        static_cast<std::size_t>(item.tie_rank)];
    if (alive != 0) {
        throw std::logic_error("introduced item already exists in active weight view");
    }
    if (!active_items_by_weight_.empty() &&
        active_item_weight_less(item, active_items_by_weight_.back())) {
        throw std::logic_error(
            "introduced items are not monotone in weight/tie-rank order");
    }
    active_items_by_weight_.push_back(item);
    alive = 1;
}

void CriticalSequence::compact_active_items_by_weight() {
    if (!active_items_by_weight_dirty_) return;
    std::size_t kept = 0;
    for (const ActiveItem& item : active_items_by_weight_) {
        if (item.tie_rank < 0 ||
            static_cast<std::size_t>(item.tie_rank) >= active_item_alive_by_rank_.size()) {
            throw std::logic_error("active weight-view item has an invalid tie rank");
        }
        if (active_item_alive_by_rank_[static_cast<std::size_t>(item.tie_rank)] == 0) {
            continue;
        }
        active_items_by_weight_[kept++] = item;
    }
    active_items_by_weight_.resize(kept);
    active_items_by_weight_dirty_ = false;
}

#ifndef NDEBUG
void CriticalSequence::assert_active_item_views_match(
    const std::vector<ActiveItem>& active_items) const {
    assert(!active_items_by_weight_dirty_);
    assert(active_items.size() == active_items_by_weight_.size());
    assert(std::is_sorted(active_items_by_weight_.begin(),
                          active_items_by_weight_.end(),
                          active_item_weight_less));

    std::vector<unsigned char> seen(item_cursors_.size(), 0);
    for (const ActiveItem& item : active_items) {
        assert(item.tie_rank >= 0);
        const std::size_t rank = static_cast<std::size_t>(item.tie_rank);
        assert(rank < seen.size());
        assert(active_item_alive_by_rank_[rank] != 0);
        assert(seen[rank] == 0);
        seen[rank] = 1;
    }
    for (const ActiveItem& item : active_items_by_weight_) {
        assert(item.tie_rank >= 0);
        const std::size_t rank = static_cast<std::size_t>(item.tie_rank);
        assert(rank < seen.size());
        assert(seen[rank] != 0);
        assert(active_item_alive_by_rank_[rank] != 0);
    }
}
#endif

void CriticalSequence::configure_item_order(const std::vector<Item>& ratio_ordered_items) {
    item_cursors_.assign(ratio_ordered_items.size(), ItemCursor{});
    active_items_by_weight_.clear();
    active_items_by_weight_.reserve(ratio_ordered_items.size());
    active_item_alive_by_rank_.assign(ratio_ordered_items.size(), 0);
    active_items_by_weight_dirty_ = false;
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

CriticalSequence::ItemCursor& CriticalSequence::cursor_for(const ActiveItem& item) {
    if (item.tie_rank < 0 ||
        static_cast<std::size_t>(item.tie_rank) >= item_cursors_.size()) {
        throw std::logic_error("active item tie rank is outside cursor table");
    }
    return item_cursors_[static_cast<std::size_t>(item.tie_rank)];
}

const CriticalSequence::ItemCursor& CriticalSequence::cursor_for(
    const ActiveItem& item) const {
    if (item.tie_rank < 0 ||
        static_cast<std::size_t>(item.tie_rank) >= item_cursors_.size()) {
        throw std::logic_error("active item tie rank is outside cursor table");
    }
    return item_cursors_[static_cast<std::size_t>(item.tie_rank)];
}

void CriticalSequence::initialize_item_cursor(const ActiveItem& item) {
#ifndef NDEBUG
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        pending_.debug_legacy_configure(item.w);
    }
#endif
    ItemCursor& cursor_state = cursor_for(item);
    cursor_state.next_built_upon = std::min<std::size_t>(1, expandable_points_.size());
    cursor_state.historical_end = expandable_points_.size();
    cursor_state.built_upon_end = std::numeric_limits<std::size_t>::max();
    cursor_state.backfill_attempts = 0;
    cursor_state.introduced = true;
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

#ifndef NDEBUG
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        if (!items.empty()) {
            Weight largest_weight = items.front().w;
            for (const ActiveItem& item : items) {
                largest_weight = std::max(largest_weight, item.w);
            }
            pending_.debug_legacy_configure(largest_weight);
        }
    }
#else
    (void)items;
#endif
}

void CriticalSequence::schedule_successors(PointId parent, Weight compute_limit,
                                           const std::vector<Item>& items,
                                           SliceBuildResult& result) {
    const State& base = state(parent);
    const Weight remaining_capacity = compute_limit - base.weight;
    long long generated = 0;
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        ++result.active_item_samples;
        result.active_items_sum += static_cast<long long>(items.size());
        result.active_items_max = std::max(
            result.active_items_max, static_cast<long long>(items.size()));
    }
    if constexpr (stats_enabled_v<StatsMode::Basic>) {
        result.successor_item_scans += static_cast<long long>(items.size());
    }
    for (std::size_t index = 0; index < items.size(); ++index) {
        const Item& item = items[index];
        if (item.w > remaining_capacity) continue;
        const Weight weight = base.weight + item.w;
        ++generated;
        pending_.store(weight, safe_add(base.profit, item.p), parent, item.id,
                       tie_rank(item, index));
    }
    if constexpr (stats_enabled_v<StatsMode::Basic>) {
        generated_candidates_ += generated;
        result.successor_attempts += generated;
        result.points_generated += generated;
    }
}

void CriticalSequence::sample_active_items(const std::vector<ActiveItem>& items,
                                           SliceBuildResult& result) {
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        ++result.active_item_samples;
        result.active_items_sum += static_cast<long long>(items.size());
        result.active_items_max = std::max(
            result.active_items_max, static_cast<long long>(items.size()));
    } else {
        (void)items;
        (void)result;
    }
}

void CriticalSequence::advance_item_cursor(const ActiveItem& item, Weight target_limit,
                                           SliceBuildResult& result) {
    ItemCursor& cursor_state = cursor_for(item);
    if (target_limit < item.w) return;
    const Weight maximum_parent_weight = target_limit - item.w;
    const std::size_t available_end = std::min(cursor_state.built_upon_end,
                                               expandable_points_.size());
    while (cursor_state.next_built_upon < available_end) {
        const std::size_t cursor = cursor_state.next_built_upon;
        const PointId parent = expandable_points_[cursor];
        const State& base = states_[parent];
        if (base.weight > maximum_parent_weight) break;

        ++cursor_state.next_built_upon;
        if constexpr (stats_enabled_v<StatsMode::Basic>) {
            ++result.cursor_advances;
            ++result.successor_item_scans;
        }
        const Weight weight = base.weight + item.w;
        if constexpr (stats_enabled_v<StatsMode::Basic>) {
            ++generated_candidates_;
            ++result.successor_attempts;
            ++result.points_generated;
        }
        if (cursor < cursor_state.historical_end) {
            if constexpr (stats_enabled_v<StatsMode::Full>) {
                ++cursor_state.backfill_attempts;
            }
            if constexpr (stats_enabled_v<StatsMode::Basic>) {
                ++result.backfill_attempts;
            }
        }
        pending_.store(weight, safe_add(base.profit, item.p), parent, item.id,
                       item.tie_rank);
    }
}

void CriticalSequence::advance_active_cursors(const std::vector<ActiveItem>& items,
                                              Weight target_limit,
                                              SliceBuildResult& result) {
    // decreasingS/active_items order is the immutable ratio tie priority.
    for (const ActiveItem& item : items) {
        advance_item_cursor(item, target_limit, result);
    }
}

void CriticalSequence::schedule_current_active_successors(
    PointId parent, Weight target_limit, const std::vector<ActiveItem>& items,
    SliceBuildResult& result) {
    if constexpr (stats_enabled_v<StatsMode::Full>) {
        sample_active_items(items, result);
    }
    const State& base = states_[parent];
    if (base.weight > target_limit) return;

    // This method is called immediately after `parent` is appended to
    // expandable_points_. For every item that can reach the current slice,
    // consuming this parent is therefore exactly the cursor advancement that
    // synchronize_active_cursors() used to reconstruct at the end of the
    // slice. Items that do not fit keep the parent as their next cursor input.
    assert(!expandable_points_.empty());
    assert(expandable_points_.back() == parent);
    const std::size_t parent_position = expandable_points_.size() - 1;
    const Weight remaining = target_limit - base.weight;
    // This mirror is ordered only for capacity filtering.  Candidate priority
    // remains the immutable tie_rank carried by each ActiveItem.
    for (const ActiveItem& item : active_items_by_weight_) {
        if (item.w > remaining) break;

        ItemCursor& cursor_state = cursor_for(item);
        assert(cursor_state.introduced);
        assert(cursor_state.next_built_upon == parent_position);
        assert(parent_position < cursor_state.built_upon_end);

        if constexpr (stats_enabled_v<StatsMode::Basic>) {
            ++result.cursor_advances;
            ++result.successor_item_scans;
            ++generated_candidates_;
            ++result.successor_attempts;
            ++result.points_generated;
        }
        pending_.store(base.weight + item.w, safe_add(base.profit, item.p),
                       parent, item.id, item.tie_rank);
        cursor_state.next_built_upon = parent_position + 1;
    }
}

#ifndef NDEBUG
void CriticalSequence::assert_active_cursors_match_legacy_sync(
    const std::vector<ActiveItem>& items, Weight target_limit,
    const std::vector<std::size_t>& legacy_sync_start_by_rank) const {
    constexpr std::size_t kUnrecordedCursor =
        std::numeric_limits<std::size_t>::max();

    for (const ActiveItem& item : items) {
        assert(item.tie_rank >= 0);
        const std::size_t rank = static_cast<std::size_t>(item.tie_rank);
        assert(rank < legacy_sync_start_by_rank.size());
        const std::size_t legacy_start = legacy_sync_start_by_rank[rank];
        assert(legacy_start != kUnrecordedCursor);
        assert(legacy_start <= expandable_points_.size());

        std::size_t legacy_result = legacy_start;
        if (target_limit >= item.w) {
            const Weight maximum_parent_weight = target_limit - item.w;
            const auto first = expandable_points_.begin() +
                static_cast<std::ptrdiff_t>(legacy_start);
            const auto position = std::upper_bound(
                first, expandable_points_.end(), maximum_parent_weight,
                [this](Weight maximum, PointId point) {
                    return maximum < states_[point].weight;
                });
            legacy_result = static_cast<std::size_t>(
                position - expandable_points_.begin());
        }

        const ItemCursor& cursor_state = cursor_for(item);
        assert(cursor_state.next_built_upon == legacy_result);
    }
}
#endif

}  // namespace ukp::faithful::detail