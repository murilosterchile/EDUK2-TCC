#include "critical_sequence.hpp"

#include <map>

namespace ukp::faithful::detail {

CriticalSequence::CriticalSequence() {
    points_.push_back(CriticalPoint{0, 0, no_point, -1, 0});
    envelope_.push_back(0);
}

const CriticalPoint& CriticalSequence::point(PointId id) const { return points_.at(id); }
PointId CriticalSequence::point_at(Weight capacity) const {
    auto position = std::upper_bound(envelope_.begin(), envelope_.end(), capacity,
        [&](Weight value, PointId id) { return value < points_[id].weight; });
    if (position == envelope_.begin()) return envelope_.front();
    return *std::prev(position);
}
Profit CriticalSequence::value_at(Weight capacity) const {
    return point(point_at(capacity)).profit;
}
const std::vector<PointId>& CriticalSequence::envelope() const noexcept { return envelope_; }
std::size_t CriticalSequence::stored_points() const noexcept { return envelope_.size(); }
long long CriticalSequence::generated_candidates() const noexcept { return generated_candidates_; }

void CriticalSequence::slice_one(const Item& item, Weight upper_bound) {
    struct Candidate { Profit profit; PointId predecessor; long long multiplicity; };
    std::map<Weight, Candidate> merged;
    for (PointId parent : envelope_) {
        const CriticalPoint& base = points_[parent];
        for (long long copies = 0; base.weight + copies * item.w <= upper_bound; ++copies) {
            ++generated_candidates_;
            const Weight weight = base.weight + copies * item.w;
            const Profit profit = safe_add(base.profit, safe_mul(copies, item.p));
            const auto found = merged.find(weight);
            if (found == merged.end() || profit > found->second.profit) {
                merged[weight] = Candidate{profit, parent, copies};
            }
        }
    }

    std::vector<PointId> next;
    Profit envelope_profit = -1;
    for (const auto& [weight, candidate] : merged) {
        if (candidate.profit <= envelope_profit) continue;
        points_.push_back(CriticalPoint{weight, candidate.profit, candidate.predecessor,
                                        item.id, candidate.multiplicity});
        next.push_back(points_.size() - 1);
        envelope_profit = candidate.profit;
    }
    envelope_ = std::move(next);
}

void CriticalSequence::build(const std::vector<Item>& items, Weight upper_bound) {
    for (const Item& item : items) slice_one(item, upper_bound);
}

void CriticalSequence::retain(const std::function<bool(PointId)>& predicate) {
    std::vector<PointId> kept;
    kept.reserve(envelope_.size());
    for (const PointId point : envelope_) {
        if (predicate(point)) kept.push_back(point);
    }
    if (kept.empty()) kept.push_back(0);
    envelope_ = std::move(kept);
}

}  // namespace ukp::faithful::detail
