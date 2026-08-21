#pragma once

#include <cstdint>

// The build system sets UKP_STATS_MODE for each faithful-solver variant.
// 0 = None, 1 = Basic, 2 = Full.  Full is the compatibility default for
// translation units that are not part of an explicitly configured variant.
#ifndef UKP_STATS_MODE
#define UKP_STATS_MODE 2
#endif

static_assert(UKP_STATS_MODE >= 0 && UKP_STATS_MODE <= 2,
              "UKP_STATS_MODE must be 0 (None), 1 (Basic), or 2 (Full)");

namespace ukp {

enum class StatsMode : std::uint8_t {
    None = 0,
    Basic = 1,
    Full = 2,
};

// Internal linkage is intentional: distinct solver-library variants are linked
// into different executables with different UKP_STATS_MODE definitions.
static constexpr StatsMode kStatsMode =
    static_cast<StatsMode>(UKP_STATS_MODE);

template <StatsMode Required>
static constexpr bool stats_enabled_v =
    static_cast<unsigned>(kStatsMode) >= static_cast<unsigned>(Required);

static constexpr const char* stats_mode_name() noexcept {
    if constexpr (kStatsMode == StatsMode::None) return "none";
    if constexpr (kStatsMode == StatsMode::Basic) return "basic";
    return "full";
}

}  // namespace ukp
