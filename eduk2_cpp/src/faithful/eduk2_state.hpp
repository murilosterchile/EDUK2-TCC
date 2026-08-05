#pragma once

#include "ukp/types.hpp"

#include <vector>

namespace ukp::faithful::detail {

// A critical point represents a strict improvement of the DP value envelope.
// Its predecessor is another critical point, so it remains sufficient to
// rebuild the associated solution after dominated states are released.
struct CriticalPoint {
    Weight weight = 0;
    Profit profit = 0;
    int item_id = -1;
    std::size_t predecessor = 0;
};

class CriticalSequence {
public:
    CriticalSequence();

    [[nodiscard]] const std::vector<CriticalPoint>& points() const noexcept;
    [[nodiscard]] const CriticalPoint& last() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    // Adds a point only if it strictly improves the profit envelope.  Such a
    // point dominates every earlier point with no smaller weight.
    bool append(CriticalPoint point);

private:
    std::vector<CriticalPoint> points_;
};

}  // namespace ukp::faithful::detail
