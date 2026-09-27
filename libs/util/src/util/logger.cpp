/*
 * Half Life 1 SDK License
 * Copyright(c) Valve Corp
 *
 * DISCLAIMER OF WARRANTIES. THE HALF LIFE 1 SDK AND ANY OTHER MATERIAL
 * DOWNLOADED BY LICENSEE IS PROVIDED "AS IS". VALVE AND ITS SUPPLIERS
 * DISCLAIM ALL WARRANTIES WITH RESPECT TO THE SDK, EITHER EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY,
 * NON-INFRINGEMENT, TITLE AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * LIMITATION OF LIABILITY. IN NO EVENT SHALL VALVE OR ITS SUPPLIERS BE LIABLE
 * FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER
 * (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
 * BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY OTHER PECUNIARY
 * LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE THE ENGINE AND/OR THE
 * SDK, EVEN IF VALVE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * For commercial use, contact: sourceengine@valvesoftware.com
 */

#include "util/logger.hpp"
#include "util/string.hpp"
#include <spdlog/common.h>
#include <spdlog/formatter.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>
#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

#include <vector>

namespace spdlog::sinks {
    class sink;
}

namespace {
    Util::LogSettings logger_settings{};
    std::shared_ptr<spdlog::logger> logger{};

    class Formatter final : public spdlog::formatter {
      public:
        void format(const spdlog::details::log_msg& msg, spdlog::memory_buf_t& dest) override
        {
            std::string_view prefix;
            switch (msg.level) {
                case spdlog::level::debug: prefix = "[DEBUG] "; break;
                case spdlog::level::warn: prefix = "[WARNING] "; break;
                case spdlog::level::err: prefix = "[ERROR] "; break;
                case spdlog::level::critical: prefix = "[FATAL ERROR] "; break;
                default: break;
            }
            if (!prefix.empty()) dest.append(prefix.data(), prefix.data() + prefix.size());
            constexpr std::size_t max_message_bytes = 8192;
            const auto length = std::min(msg.payload.size(), max_message_bytes);
            dest.append(msg.payload.data(), msg.payload.data() + length);
            if (msg.payload.size() > length) {
                constexpr std::string_view suffix = " [log message truncated]\n";
                dest.append(suffix.data(), suffix.data() + suffix.size());
            }
            else if (msg.level != spdlog::level::info) dest.push_back('\n');

#ifdef _WIN32
            if (!prefix.empty()) {
                msg.color_range_start = 0;
                msg.color_range_end = prefix.size() - 1;
            }
#endif
        }

        [[nodiscard]] std::unique_ptr<formatter> clone() const override
        {
            return spdlog::details::make_unique<Formatter>();
        }
    };

    /**
     * @brief Custom sink for spdlog that uses a LogOutput for message handling.
     */
    template <typename Mutex>
    class LogOutputSink final : public spdlog::sinks::base_sink<Mutex> {
      public:
        /**
         * @brief Construct a new Custom Sink object.
         *
         * @param log_output The LogOutput instance to handle the actual logging.
         */
        explicit LogOutputSink(std::shared_ptr<Util::LogOutput> log_output) : log_output_(std::move(log_output))
        {
        }

      protected:
        /**
         * @brief Process a log message.
         *
         * Called by spdlog to process each log message.
         * It formats the message and passes it to the LogOutput instance.
         *
         * @param msg The log message to be processed.
         */
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            spdlog::memory_buf_t formatted;
            spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
            log_output_->write_log(std::string_view{formatted.data(), formatted.size()});
        }

        /**
         * @brief Flush the sink.
         *
         * Called by spdlog when the sink needs to be flushed.
         */
        void flush_() override
        {
            // The view must implement this
        }

      private:
        ///< The LogOutput instance handling the actual logging.
        std::shared_ptr<Util::LogOutput> log_output_;
    };
    // Optional diagnostic output must not abort the game on an I/O failure.
    class ResilientFileSink final : public spdlog::sinks::base_sink<std::mutex> {
      public:
        explicit ResilientFileSink(const std::string& path)
        {
            file_ = std::fopen(path.c_str(), "ab");
            if (!file_) { report_failure(); return; }
            (void)std::setvbuf(file_, buffer_.data(), _IOFBF, buffer_.size());
        }
        ResilientFileSink(const ResilientFileSink&) = delete;
        ResilientFileSink& operator=(const ResilientFileSink&) = delete;
        ~ResilientFileSink() override { if (file_) std::fclose(file_); }
      protected:
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            if (!file_) return;
            spdlog::memory_buf_t formatted;
            formatter_->format(msg, formatted);
            if (std::fwrite(formatted.data(), 1, formatted.size(), file_) != formatted.size()) {
                disable(); return;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - last_flush_ >= std::chrono::seconds{1}) { flush_(); last_flush_ = now; }
        }
        void flush_() override { if (file_ && std::fflush(file_) != 0) disable(); }
      private:
        static void report_failure()
        {
            std::fprintf(stderr, "[ReHLDS-M] Optional file logging disabled: %s\n", std::strerror(errno));
        }
        void disable()
        {
            report_failure();
            auto* file = file_; file_ = nullptr;
            if (file) std::fclose(file);
        }
        std::FILE* file_{};
        std::array<char, 64 * 1024> buffer_{};
        std::chrono::steady_clock::time_point last_flush_{};
    };

}

namespace Util {
    void log_init(const LogSettings& settings)
    {
        logger_settings = settings;
        spdlog::drop_all();

        std::vector<spdlog::sink_ptr> sinks{};
        constexpr std::size_t max_sinks = 2;
        sinks.reserve(max_sinks);

        if ((settings.output & LogDestination::view) == LogDestination::view) {
            sinks.emplace_back(std::make_shared<LogOutputSink<std::mutex>>(settings.log_output));
        }

        if ((settings.output & LogDestination::file) == LogDestination::file) {
            sinks.emplace_back(std::make_shared<ResilientFileSink>(settings.logfile));
        }

        logger = std::make_shared<spdlog::logger>("main", sinks.cbegin(), sinks.cend());
        logger->flush_on(static_cast<spdlog::level::level_enum>(settings.flush_on));
        logger->set_level(static_cast<spdlog::level::level_enum>(settings.level));
        logger->set_formatter(std::make_unique<Formatter>());

        register_logger(logger);
        set_default_logger(logger);
    }

    void log_init(const bool con_debug, std::string logfile, std::shared_ptr<LogOutput> log_output)
    {
        LogSettings settings{};
        settings.log_output = std::move(log_output);

        if (con_debug) {
            settings.logfile = std::move(logfile);
            settings.output = LogDestination::view_and_file;
        }

        log_init(settings);
    }

    const LogSettings& log_settings()
    {
        return logger_settings;
    }
}
