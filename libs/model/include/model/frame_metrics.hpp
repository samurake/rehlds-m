#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace Model {
    struct FrameSummary {
        std::uint64_t frames{}, over_budget_frames{};
        std::size_t samples{};
        double frame_p50_us{}, frame_p95_us{}, frame_p99_us{}, engine_p99_us{}, wake_p99_us{}, max_frame_us{};
    };

    // One owner (the game thread); bounded storage and no allocation per sample.
    // Percentiles cover the latest 2048 launcher iterations, never claim engine ticks.
    class FrameMetrics final {
      public:
        static constexpr std::size_t CAPACITY = 2048;
        void engine_time(std::chrono::nanoseconds elapsed) noexcept { engine_ns_ = elapsed.count(); }
        void record(std::chrono::nanoseconds frame, std::chrono::nanoseconds wake, bool over_budget) noexcept
        {
            const auto index = static_cast<std::size_t>(frames_ % CAPACITY);
            frames_ns_[index] = frame.count();
            engine_times_ns_[index] = engine_ns_;
            wake_ns_[index] = std::max<std::int64_t>(0, wake.count());
            max_frame_ns_ = (std::max)(max_frame_ns_, frame.count());
            ++frames_;
            over_budget_frames_ += over_budget ? 1 : 0;
        }
        [[nodiscard]] FrameSummary summary() const
        {
            FrameSummary s{};
            s.frames = frames_; s.over_budget_frames = over_budget_frames_;
            s.samples = static_cast<std::size_t>(std::min<std::uint64_t>(frames_, CAPACITY));
            if (s.samples == 0) return s;
            auto frames = frames_ns_;
            std::sort(frames.begin(), frames.begin() + s.samples);
            s.frame_p50_us = percentile(frames, s.samples, 50);
            s.frame_p95_us = percentile(frames, s.samples, 95);
            s.frame_p99_us = percentile(frames, s.samples, 99);
            auto engine = engine_times_ns_;
            std::sort(engine.begin(), engine.begin() + s.samples);
            s.engine_p99_us = percentile(engine, s.samples, 99);
            auto wake = wake_ns_;
            std::sort(wake.begin(), wake.begin() + s.samples);
            s.wake_p99_us = percentile(wake, s.samples, 99);
            s.max_frame_us = static_cast<double>(max_frame_ns_) / 1000.0;
            return s;
        }
      private:
        static double percentile(const std::array<std::int64_t, CAPACITY>& values,
                                 std::size_t size, std::size_t percent)
        {
            return static_cast<double>(values[(size * percent + 99) / 100 - 1]) / 1000.0;
        }
        std::array<std::int64_t, CAPACITY> frames_ns_{}, engine_times_ns_{}, wake_ns_{};
        std::int64_t engine_ns_{}, max_frame_ns_{};
        std::uint64_t frames_{}, over_budget_frames_{};
    };
}
