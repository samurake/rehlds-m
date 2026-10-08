#pragma once

#include <spdlog/sinks/sink.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace Util {
    struct OutputStats {
        std::uint64_t pending{}, high_water{}, dropped{}, write_failures{};
        bool asynchronous{};
    };

    // One consumer; bounded, preallocated payloads; no I/O while holding the queue lock.
    class AsyncLogSink final : public spdlog::sinks::sink {
      public:
        AsyncLogSink(std::vector<spdlog::sink_ptr> sinks, unsigned capacity, unsigned flush_ms);
        ~AsyncLogSink() override;
        void log(const spdlog::details::log_msg& msg) override;
        void flush() override;
        void set_pattern(const std::string& pattern) override;
        void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override;
        bool flush_and_wait(std::chrono::milliseconds timeout);
        bool stop(std::chrono::milliseconds timeout);
        OutputStats stats() const;

      private:
        struct State;
        static void consume(const std::shared_ptr<State>& state);
        std::shared_ptr<State> state_;
        std::thread worker_{};
    };
}
