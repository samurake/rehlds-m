#pragma once

#include "model/frame_metrics.hpp"
#include "util/async_log_sink.hpp"
#include <cstdint>
#include <memory>
#include <thread>

namespace spdlog {
    class logger;
}

namespace Model {
    struct ReportContext {
        float engine_fps{};
        std::uint64_t input_high_water{}, input_rejected{}, completion_rejected{};
        Util::OutputStats output{};
    };

    // One engine-thread producer and one reporting worker. Submitted snapshots
    // own their storage; sorting and logging never call engine/entity APIs.
    class MetricsReporter final {
      public:
        MetricsReporter();
        explicit MetricsReporter(std::shared_ptr<spdlog::logger> logger);
        ~MetricsReporter();
        // Nonblocking, best-effort submission. False means contention, a full
        // two-slot queue, or shutdown; lifetime metrics remain with the producer.
        bool submit(const FrameMetrics& metrics, ReportContext context);
        std::uint64_t rejected() const;
        std::uint64_t failures() const;

      private:
        struct State;
        static void consume(const std::shared_ptr<State>& state);
        std::shared_ptr<State> state_;
        std::thread worker_{};
    };
}
