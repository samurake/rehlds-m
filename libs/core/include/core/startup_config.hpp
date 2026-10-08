#pragma once

#include "core/cmdline_args.hpp"
#include <array>
#include <filesystem>
#include <string>

namespace Core {
    // Immutable after startup. Engine server.cfg is deliberately a separate domain.
    struct StartupSettings {
        int target_fps{};
        unsigned frame_stats_seconds{};
        bool async_output{};
        unsigned output_queue_capacity{1024};
        unsigned output_flush_ms{100};
        std::array<std::string, 5> sources{"default", "default", "default", "default", "default"};
        std::filesystem::path config_path{};
        bool config_found{};
    };

    // Resolve beside the running executable, not the current working directory.
    // An empty result is an error; the caller must not fall back to another cfg.
    std::filesystem::path startup_config_path();
    // Reset settings, then parse a bounded file and the supported CLI fallback.
    // Consume settings only on success; error describes the rejected input.
    bool load_startup_settings(
      const CmdLineArgs& args, const std::filesystem::path& path, StartupSettings& settings, std::string& error);
    // Strip launcher-only arguments and configure engine ticrate for target mode.
    void apply_startup_settings(CmdLineArgs& args, const StartupSettings& settings);
    void report_startup_settings(const StartupSettings& settings);
}
