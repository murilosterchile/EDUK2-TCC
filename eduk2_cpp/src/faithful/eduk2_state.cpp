#include "eduk2_state.hpp"

namespace ukp::faithful::detail {

CriticalSequence::CriticalSequence() {
    points_.push_back(CriticalPoint{});
}

const std::vector<CriticalPoint>& CriticalSequence::points() const noexcept {
    return points_;
}

const CriticalPoint& CriticalSequence::last() const noexcept {
    return points_.back();
}

bool CriticalSequence::empty() const noexcept {
    return points_.empty();
}

bool CriticalSequence::append(CriticalPoint point) {
    if (point.profit <= points_.back().profit) return false;
    points_.push_back(point);
    return true;
}

}  // namespace ukp::faithful::detail
