#include "model/metrics_reporter.hpp"
#include <spdlog/spdlog.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace Model {
    struct MetricsReporter::State {
        explicit State(std::shared_ptr<spdlog::logger> destination) : logger(std::move(destination))
        {
        }

        struct Report {
            FrameMetrics metrics{};
            ReportContext context{};
        };

        std::array<Report, 2> reports{};
        std::size_t head{}, size{};
        std::mutex mutex{};
        std::condition_variable ready{}, finished{};
        std::atomic<std::uint64_t> rejected{}, failures{};
        bool stopping{}, done{};
        std::shared_ptr<spdlog::logger> logger;

        void write(const Report& report)
        {
            if (!logger)
                return;
            const auto s = report.metrics.summary();
            const auto& c = report.context;
            logger->info(
              "[ReHLDS-M] launcher_iterations={} window={} frame_us(p50/p95/p99)={:.1f}/{:.1f}/{:.1f} "
              "engine_call_p99_us={:.1f} wake_p99_us={:.1f} work_p99_us={:.1f} launcher_work_p99_us={:.1f} "
              "window_max_frame_us={:.1f} max_frame_us={:.1f} window_over_budget={} over_budget_frames={} "
              "lifetime_launcher_gt_1ms={} lifetime_launcher_gt_5ms={} "
              "lifetime_launcher_gt_10ms={} lifetime_launcher_gt_50ms={} "
              "engine_reported_fps={:.1f} input_high_water={} input_rejected={} completion_rejected={} "
              "output_async={} output_pending={} output_high_water={} output_dropped={} "
              "output_write_failures={} reports_rejected={} reports_failed={}\n",
              s.frames, s.samples, s.frame_p50_us, s.frame_p95_us, s.frame_p99_us, s.engine_p99_us, s.wake_p99_us,
              s.work_p99_us, s.launcher_work_p99_us, s.window_max_frame_us, s.max_frame_us, s.window_over_budget,
              s.over_budget_frames, s.lifetime_stalls[0], s.lifetime_stalls[1], s.lifetime_stalls[2],
              s.lifetime_stalls[3], c.engine_fps, c.input_high_water, c.input_rejected, c.completion_rejected,
              c.output.asynchronous, c.output.pending, c.output.high_water, c.output.dropped, c.output.write_failures,
              rejected.load(), failures.load());
        }
    };

    MetricsReporter::MetricsReporter() : MetricsReporter(spdlog::default_logger())
    {
    }

    MetricsReporter::MetricsReporter(std::shared_ptr<spdlog::logger> logger) :
      state_(std::make_shared<State>(std::move(logger)))
    {
        worker_ = std::thread{[state = state_] {
            consume(state);
        }};
    }

    void MetricsReporter::consume(const std::shared_ptr<State>& state)
    {
        State::Report report;
        try {
            for (;;) {
                {
                    std::unique_lock lock{state->mutex};
                    state->ready.wait(lock, [&] {
                        return state->stopping || state->size;
                    });
                    if (!state->size)
                        break;
                    report = state->reports[state->head];
                    state->head = (state->head + 1) % state->reports.size();
                    --state->size;
                }
                try {
                    state->write(report);
                }
                catch (...) {
                    ++state->failures;
                }
            }
        }
        catch (...) {
            ++state->failures;
        }
        // Shared state also owns the logger if shutdown times out and detaches.
        {
            const std::scoped_lock lock{state->mutex};
            state->stopping = true;
            state->done = true;
            state->rejected.fetch_add(state->size);
            state->size = 0;
        }
        state->finished.notify_all();
    }

    MetricsReporter::~MetricsReporter()
    {
        std::unique_lock lock{state_->mutex};
        state_->stopping = true;
        state_->ready.notify_one();
        const auto done = state_->finished.wait_for(lock, std::chrono::milliseconds{1000}, [&] {
            return state_->done;
        });
        lock.unlock();
        if (done)
            worker_.join();
        else
            worker_.detach();
    }

    bool MetricsReporter::submit(const FrameMetrics& metrics, ReportContext context)
    {
        std::unique_lock lock{state_->mutex, std::try_to_lock};
        if (!lock || state_->stopping || state_->size == state_->reports.size()) {
            ++state_->rejected;
            return false;
        }
        auto& report = state_->reports[(state_->head + state_->size) % state_->reports.size()];
        const bool wake = state_->size == 0;
        report.metrics = metrics;
        report.context = context;
        ++state_->size;
        lock.unlock();
        if (wake)
            state_->ready.notify_one();
        return true;
    }

    std::uint64_t MetricsReporter::rejected() const
    {
        return state_->rejected.load();
    }

    std::uint64_t MetricsReporter::failures() const
    {
        return state_->failures.load();
    }
}
