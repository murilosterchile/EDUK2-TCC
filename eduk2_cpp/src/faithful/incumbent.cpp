#include "incumbent.hpp"

namespace ukp::faithful::detail {

Incumbent::Incumbent(std::size_t item_count) : multiplicity_by_id(item_count, 0) {}

bool Incumbent::consider(Profit candidate_profit, Weight candidate_weight,
                         std::vector<long long> candidate_multiplicity,
                         PointId candidate_origin, int candidate_residual_item,
                         long long candidate_residual_count) {
    if (candidate_profit < profit ||
        (candidate_profit == profit && candidate_weight <= weight)) return false;
    profit = candidate_profit;
    weight = candidate_weight;
    multiplicity_by_id = std::move(candidate_multiplicity);
    origin = candidate_origin;
    residual_item_id = candidate_residual_item;
    residual_count = candidate_residual_count;
    return true;
}

Solution Incumbent::solution(std::string solver_name) const {
    Solution out;
    out.profit = profit;
    out.weight = weight;
    out.multiplicity_by_id = multiplicity_by_id;
    out.optimal = true;
    out.solver_name = std::move(solver_name);
    return out;
}

}  // namespace ukp::faithful::detail
