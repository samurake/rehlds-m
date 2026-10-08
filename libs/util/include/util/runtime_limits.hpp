#pragma once

#include <cstddef>

namespace Util::RuntimeLimits {
    // Fixed safety policy, not configurable by flags or rehldsm.cfg.
    inline constexpr int max_target_fps = 2000;
    inline constexpr unsigned max_frame_stats_seconds = 3600;
    inline constexpr unsigned min_output_capacity = 64;
    inline constexpr unsigned max_output_capacity = 4096;
    inline constexpr unsigned min_output_flush_ms = 10;
    inline constexpr unsigned max_output_flush_ms = 1000;
    inline constexpr std::size_t max_config_bytes = 64 * 1024;
}
