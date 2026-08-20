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
    nonzero_item_ids.clear();
    for (std::size_t id = 0; id < multiplicity_by_id.size(); ++id) {
        if (multiplicity_by_id[id] != 0) nonzero_item_ids.push_back(static_cast<int>(id));
    }
    origin = candidate_origin;
    residual_item_id = candidate_residual_item;
    residual_count = candidate_residual_count;
    return true;
}

bool Incumbent::consider_sparse(Profit candidate_profit, Weight candidate_weight,
                                const std::vector<long long>& candidate_multiplicity,
                                const std::vector<int>& candidate_nonzero_item_ids,
                                PointId candidate_origin, int candidate_residual_item,
                                long long candidate_residual_count) {
    if (candidate_profit < profit ||
        (candidate_profit == profit && candidate_weight <= weight)) return false;
    if (candidate_multiplicity.size() != multiplicity_by_id.size()) {
        throw std::invalid_argument("candidate multiplicity has the wrong size");
    }

    for (const int item_id : nonzero_item_ids) {
        multiplicity_by_id[static_cast<std::size_t>(item_id)] = 0;
    }
    nonzero_item_ids.clear();
    nonzero_item_ids.reserve(candidate_nonzero_item_ids.size());
    for (const int item_id : candidate_nonzero_item_ids) {
        if (item_id < 0 || static_cast<std::size_t>(item_id) >= multiplicity_by_id.size()) {
            throw std::out_of_range("candidate nonzero item id is outside multiplicity");
        }
        const long long count = candidate_multiplicity[static_cast<std::size_t>(item_id)];
        if (count == 0) continue;
        multiplicity_by_id[static_cast<std::size_t>(item_id)] = count;
        nonzero_item_ids.push_back(item_id);
    }

    profit = candidate_profit;
    weight = candidate_weight;
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
