#include "ukp/verify.hpp"
#include <algorithm>
#include <vector>

namespace ukp {

bool verify_solution(const Instance& inst, const Solution& sol) {
    if (sol.multiplicity_by_id.size() < inst.items.size()) return false;
    Weight w = 0;
    Profit p = 0;
    for (const auto& it : inst.items) {
        long long m = sol.multiplicity_by_id[it.id];
        if (m < 0) return false;
        w += m * it.w;
        p += m * it.p;
    }
    return w == sol.weight && p == sol.profit && w <= inst.capacity;
}

Profit dense_dp_value(const Instance& inst) {
    std::vector<Profit> dp(static_cast<size_t>(inst.capacity) + 1, 0);
    for (Weight y = 1; y <= inst.capacity; ++y) {
        Profit best = dp[static_cast<size_t>(y - 1)];
        for (const auto& it : inst.items) {
            if (it.w <= y) best = std::max(best, dp[static_cast<size_t>(y - it.w)] + it.p);
        }
        dp[static_cast<size_t>(y)] = best;
    }
    return dp[static_cast<size_t>(inst.capacity)];
}

}  // namespace ukp
