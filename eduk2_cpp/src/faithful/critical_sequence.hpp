#pragma once

#include "ukp/types.hpp"

#include <cstddef>
#include <functional>
#include <map>
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
    long long states_created = 0;
    long long points_generated = 0;
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
    };

    void schedule_successors(PointId parent, Weight compute_limit,
                             const std::vector<Item>& items, SliceBuildResult& result);

    std::vector<State> states_;
    std::vector<PointId> skip_points_;
    std::map<Weight, Candidate> pending_;
    bool root_processed_ = false;
    long long generated_candidates_ = 0;
};

}  // namespace ukp::faithful::detail
