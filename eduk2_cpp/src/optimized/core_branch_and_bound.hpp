#pragma once

#include "ukp/types.hpp"

#include <cstddef>
#include <vector>

namespace ukp::optimized::detail {

struct CoreBBResult {
    Profit profit = 0;
    Weight weight = 0;
    std::vector<long long> multiplicity_by_id;
    long long nodes = 0;
    bool hit_limit = false;
    bool closed_gap = false;
};

CoreBBResult run_core_branch_and_bound(std::vector<Item> items,
                                       Weight capacity,
                                       long long node_limit,
                                       int requested_core_size,
                                       Profit incumbent,
                                       Profit global_upper,
                                       std::size_t original_item_count);

class AdaptiveBBController {
public:
    explicit AdaptiveBBController(long long configured_limit);

    bool should_escalate(const CoreBBResult& probe,
                         Profit old_incumbent,
                         Profit global_upper,
                         long long items_after_preprocess) const;

    long long probe_nodes = 0;
    long long max_nodes = 0;
    bool enabled = true;
};

}  // namespace ukp::optimized::detail
