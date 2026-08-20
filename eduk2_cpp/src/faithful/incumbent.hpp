#pragma once

#include "critical_sequence.hpp"
#include "ukp/types.hpp"

namespace ukp::faithful::detail {

// A reconstructible lower bound.  Besides the complete multiplicity vector it
// records how it was obtained from a critical point plus a greedy residual
// extension, which is required when that point is later fathomed away.
struct Incumbent {
    Profit profit = 0;
    Weight weight = 0;
    std::vector<long long> multiplicity_by_id;
    std::vector<int> nonzero_item_ids;
    PointId origin = no_point;
    int residual_item_id = -1;
    long long residual_count = 0;

    explicit Incumbent(std::size_t item_count = 0);

    bool consider(Profit candidate_profit, Weight candidate_weight,
                  std::vector<long long> candidate_multiplicity,
                  PointId candidate_origin = no_point,
                  int candidate_residual_item = -1,
                  long long candidate_residual_count = 0);

    // Updates only entries known to be nonzero in the reconstructed candidate.
    // This avoids copying the full item-sized vector for every DP incumbent.
    bool consider_sparse(Profit candidate_profit, Weight candidate_weight,
                         const std::vector<long long>& candidate_multiplicity,
                         const std::vector<int>& candidate_nonzero_item_ids,
                         PointId candidate_origin = no_point,
                         int candidate_residual_item = -1,
                         long long candidate_residual_count = 0);

    [[nodiscard]] Solution solution(std::string solver_name) const;
};

}  // namespace ukp::faithful::detail
