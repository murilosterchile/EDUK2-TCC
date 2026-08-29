#pragma once

#include "ukp/terminating_step_off.hpp"
#include "ukp/types.hpp"

#include <cstddef>

namespace ukp::optimized::detail {

// AUTO is deliberately asymmetric: a false TSO choice can be far more
// expensive than an EDUK2 false negative.  The region below is therefore a
// high-confidence structural region, not a characterization of every instance
// on which TSO can win.
inline constexpr long long kDispatcherMaxItems = 10'000;

// Every surviving item must be within 0.05% of the best efficiency.
inline constexpr long long kDispatcherEfficiencyBandDenominator = 2000;

// The best-efficiency item must lie within 5% of one endpoint of the weight
// interval.  Both light-end and heavy-end orientations are supported.
inline constexpr long long kDispatcherEndpointSlackDenominator = 20;

// Calibrated high-confidence weight-shape gate: w_max / w_min <= 1.42.
inline constexpr long long kDispatcherWeightSpanNumerator = 71;
inline constexpr long long kDispatcherWeightSpanDenominator = 50;

// Residual pressure must be at least 0.70.  For a light-end best item this is
// (C mod w_best)/w_best; for a heavy-end best item it is the complementary
// distance to the next multiple of w_best.
inline constexpr long long kDispatcherResidualPressureNumerator = 7;
inline constexpr long long kDispatcherResidualPressureDenominator = 10;

// A finite transition cap below this value is treated as a safety mode and
// AUTO does not speculate with TSO.  Corpus calibration showed useful
// high-confidence candidates consuming up to about 601M transitions.  Zero is
// still the unlimited calibration mode.
inline constexpr long long kDispatcherMinFiniteTransitionBudget = 650'000'000;

enum class KernelChoice { Eduk2, Tso };
enum class BestWeightOrientation { Unknown, Light, Heavy };

struct DispatchDecision {
    KernelChoice kernel = KernelChoice::Eduk2;
    const char* reason = "conservative_default";
    BestWeightOrientation orientation = BestWeightOrientation::Unknown;
    const char* orientation_name = "unknown";
    std::size_t estimated_tso_bytes = 0;
    long double estimated_tso_work = 0.0L;
    long double residual_pressure = 0.0L;
};

DispatchDecision dispatch_kernel(
    const InstanceFeatures& features, const TsoOptions& tso_options);

}  // namespace ukp::optimized::detail