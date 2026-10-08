#include "core/startup_config.hpp"
#include "util/logger.hpp"
#include "util/runtime_limits.hpp"
#include <charconv>
#include <fstream>
#include <optional>
#include <string_view>
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <Windows.h>
#else
  #include <unistd.h>
#endif

namespace {
    enum Setting : std::size_t {
        target_fps,
        frame_stats_seconds,
        async_output,
        output_queue_capacity,
        output_flush_ms,
        setting_count
    };

    constexpr std::array<std::string_view, setting_count> keys{
      "target_fps", "frame_stats_seconds", "async_output", "output_queue_capacity", "output_flush_ms"};

    std::string_view trim(std::string_view value)
    {
        const auto first = value.find_first_not_of(" \t\r");
        if (first == value.npos)
            return {};
        return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
    }

    bool integer(std::string_view value, unsigned minimum, unsigned maximum, unsigned& result)
    {
        const auto [end, code] = std::from_chars(value.data(), value.data() + value.size(), result);
        return code == std::errc{} && end == value.data() + value.size() && result >= minimum && result <= maximum;
    }
}

namespace Core {
    std::filesystem::path startup_config_path()
    {
#ifdef _WIN32
        std::array<wchar_t, 32768> buffer{};
        const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length || length >= buffer.size())
            return {};
        return std::filesystem::path{
                 std::wstring_view{buffer.data(), length}
        }
                 .parent_path() /
               "rehldsm.cfg";
#else
        std::array<char, 4096> buffer{};
        const auto length = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length <= 0 || static_cast<std::size_t>(length) >= buffer.size())
            return {};
        return std::filesystem::path{
                 std::string_view{buffer.data(), static_cast<std::size_t>(length)}
        }
                 .parent_path() /
               "rehldsm.cfg";
#endif
    }

    bool load_startup_settings(
      const CmdLineArgs& args, const std::filesystem::path& path, StartupSettings& settings, std::string& error)
    {
        settings = StartupSettings{};
        settings.config_path = path;
        error.clear();
        if (args.contains("targetfps")) {
            error = "-targetfps has been removed. Remove it from the launch options and set target_fps (0-2000) in "
                    "rehldsm.cfg.";
            return false;
        }
        if (path.empty()) {
            error = "Cannot resolve the executable directory for rehldsm.cfg.";
            return false;
        }
        std::array<std::optional<std::string>, keys.size()> values;
        std::error_code ec;
        settings.config_found = std::filesystem::exists(path, ec);
        if (ec) {
            error = "Cannot inspect " + path.string() + ": " + ec.message();
            return false;
        }
        if (settings.config_found) {
            std::ifstream file{path, std::ios::binary};
            if (!file) {
                error = "Cannot open " + path.string();
                return false;
            }
            // Read no more than the limit plus one byte, even for a growing file.
            std::string contents(Util::RuntimeLimits::max_config_bytes + 1, '\0');
            file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            const auto size = static_cast<std::size_t>(file.gcount());
            if (size > Util::RuntimeLimits::max_config_bytes || file.bad()) {
                error = "rehldsm.cfg exceeds 64 KiB or could not be read.";
                return false;
            }
            contents.resize(size);
            if (contents.starts_with("\xEF\xBB\xBF"))
                contents.erase(0, 3);
            if (contents.find('\0') != contents.npos) {
                error = "rehldsm.cfg contains a NUL byte.";
                return false;
            }
            std::string_view remaining{contents};
            unsigned line_number{};
            while (!remaining.empty()) {
                ++line_number;
                const auto end = remaining.find('\n');
                auto line = remaining.substr(0, end);
                remaining = end == remaining.npos ? std::string_view{} : remaining.substr(end + 1);
                line = trim(line.substr(0, line.find('#')));
                if (line.empty())
                    continue;
                const auto equals = line.find('=');
                const auto key = trim(line.substr(0, equals));
                std::size_t index{};
                while (index < keys.size() && keys[index] != key)
                    ++index;
                if (equals == line.npos || index == keys.size() || values[index]) {
                    error =
                      "rehldsm.cfg line " + std::to_string(line_number) + ": unknown, duplicate or malformed setting.";
                    return false;
                }
                values[index] = std::string{trim(line.substr(equals + 1))};
                settings.sources[index] = "rehldsm.cfg";
            }
        }
        // Target pacing is cfg-only; retain the existing report-interval fallback.
        if (!values[frame_stats_seconds] && args.contains("framestats")) {
            values[frame_stats_seconds] = args.get_argument_option("framestats").value_or("");
            settings.sources[frame_stats_seconds] = "command line";
        }
        unsigned value{};
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (!values[i])
                continue;
            bool valid{};
            switch (i) {
                case target_fps:
                    valid = integer(*values[i], 0, Util::RuntimeLimits::max_target_fps, value);
                    settings.target_fps = static_cast<int>(value);
                    break;
                case frame_stats_seconds:
                    valid = integer(*values[i], 0, Util::RuntimeLimits::max_frame_stats_seconds, value);
                    settings.frame_stats_seconds = value;
                    break;
                case async_output:
                    valid = *values[i] == "true" || *values[i] == "false";
                    settings.async_output = *values[i] == "true";
                    break;
                case output_queue_capacity:
                    valid = integer(
                      *values[i], Util::RuntimeLimits::min_output_capacity, Util::RuntimeLimits::max_output_capacity,
                      value);
                    settings.output_queue_capacity = value;
                    break;
                case output_flush_ms:
                    valid = integer(
                      *values[i], Util::RuntimeLimits::min_output_flush_ms, Util::RuntimeLimits::max_output_flush_ms,
                      value);
                    settings.output_flush_ms = value;
                    break;
                default: break;
            }
            // Retain the existing stricter nonzero CLI contract.
            if (i == frame_stats_seconds && settings.sources[i] == "command line" && value == 0)
                valid = false;
            if (!valid) {
                constexpr std::array<std::string_view, 5> ranges{
                  "0-2000", "0-3600", "true or false", "64-4096", "10-1000"};
                error = "Invalid " + std::string{keys[i]} + " from " + settings.sources[i] + "; expected " +
                        std::string{ranges[i]};
                return false;
            }
        }
        if (!settings.target_fps && args.contains("pingboost")) {
            const auto level = args.get_argument_option_as<int>("pingboost");
            if (!level || *level < 0 || *level > 5) {
                error = "-pingboost requires an integer between 0 and 5.";
                return false;
            }
        }
        return true;
    }

    void apply_startup_settings(CmdLineArgs& args, const StartupSettings& settings)
    {
        // Launcher-only report flags do not need to reach the engine either.
        args.remove("framestats");
        if (settings.target_fps) {
            args.remove("pingboost");
            args.push_back("+sys_ticrate", "0");
        }
    }

    void report_startup_settings(const StartupSettings& s)
    {
        Util::log_info(
          "[ReHLDS-M] startup config: {} ({}) -- loaded only at startup; restart to reload.\n", s.config_path.string(),
          s.config_found ? "loaded" : "missing, using CLI/defaults");
        Util::log_info(
          "[ReHLDS-M] target_fps={} [{}] frame_stats_seconds={} [{}] async_output={} [{}] "
          "output_queue_capacity={} [{}] output_flush_ms={} [{}]\n",
          s.target_fps, s.sources[0], s.frame_stats_seconds, s.sources[1], s.async_output, s.sources[2],
          s.output_queue_capacity, s.sources[3], s.output_flush_ms, s.sources[4]);
        if (s.target_fps)
            Util::log_info(
              "[ReHLDS-M] Target pacing requires sys_ticrate 0 and fps_max 0 in the engine's server.cfg.\n");
    }
}
