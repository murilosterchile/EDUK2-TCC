#pragma once

#include "ukp/types.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace ukp::faithful::detail {

using PointId = std::size_t;
inline constexpr PointId no_point = static_cast<PointId>(-1);

struct CriticalPoint {
    Weight weight = 0;
    Profit profit = 0;
    PointId predecessor = no_point;
    int last_item = -1;
    long long multiplicity = 0;
};

class CriticalSequence {
public:
    CriticalSequence();

    void slice_one(const Item& item, Weight upper_bound);
    void build(const std::vector<Item>& items, Weight upper_bound);
    void retain(const std::function<bool(PointId)>& predicate);

    [[nodiscard]] const CriticalPoint& point(PointId id) const;
    [[nodiscard]] PointId point_at(Weight capacity) const;
    [[nodiscard]] Profit value_at(Weight capacity) const;
    [[nodiscard]] const std::vector<PointId>& envelope() const noexcept;
    [[nodiscard]] std::size_t stored_points() const noexcept;
    [[nodiscard]] long long generated_candidates() const noexcept;

private:
    std::vector<CriticalPoint> points_;
    std::vector<PointId> envelope_;
    long long generated_candidates_ = 0;
};

}  // namespace ukp::faithful::detail
