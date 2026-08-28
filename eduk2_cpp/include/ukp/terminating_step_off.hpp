#pragma once

#include "ukp/types.hpp"

#include <cstddef>
#include <string>

namespace ukp::optimized {

class Solver;

enum class TsoStatus {
    ProvedOptimal,
    KernelNotApplicable
};

struct TsoTelemetry {
    long long original_items = 0;
    long long after_common_preprocessing_items = 0;
    Weight capacity = 0;
    Weight last_capacity_scanned = 0;
    Weight termination_level = 0;
    Weight best_item_weight = 0;
    Weight weight_gcd = 0;
    long long states_scanned = 0;
    long long transitions_considered = 0;
    long long transitions_improved = 0;
    long long ties_reassigned = 0;
    std::size_t estimated_dp_bytes = 0;
    bool terminated_early = false;
};

struct TsoOptions {
    // A caller may deliberately raise this for controlled experiments.  The
    // default prevents an accidental multi-gigabyte capacity-indexed DP.
    std::size_t max_dp_bytes = 512ULL * 1024ULL * 1024ULL;
};

struct TsoResult {
    Solution solution;
    TsoTelemetry telemetry;
    TsoStatus status = TsoStatus::KernelNotApplicable;
    std::string status_message = "kernel_not_applicable";
};

class TerminatingStepOff {
public:
    explicit TerminatingStepOff(TsoOptions options = {});
    TsoResult solve(const Instance& instance) const;

private:
    friend class Solver;
    TsoResult solve_with_common_items(
        const Instance& instance, std::vector<Item> common_items) const;
    TsoOptions options_;
};

}  // namespace ukp::optimized
