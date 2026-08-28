#pragma once

#include "ukp/terminating_step_off.hpp"
#include "ukp/types.hpp"

#include <cstddef>
#include <string>

namespace ukp::optimized::detail {

enum class KernelChoice { Eduk2, Tso };

struct DispatchDecision {
    KernelChoice kernel = KernelChoice::Eduk2;
    std::string reason = "conservative_default";
    std::size_t estimated_tso_bytes = 0;
    long double estimated_tso_work = 0.0L;
};

DispatchDecision dispatch_kernel(
    const InstanceFeatures& features, const TsoOptions& tso_options);

}  // namespace ukp::optimized::detail
