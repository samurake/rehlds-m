#include "model/metrics_reporter.hpp"
#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace std::chrono_literals;

TEST(MetricsReporterTest, ThrowingLoggerDoesNotEscapeWorkerAndFailureIsCounted)
{
    auto destination = std::make_shared<spdlog::logger>("faulty-metrics");
    // Force a failure in formatting, then an error handler that itself throws.
    destination->set_error_handler([](const std::string&) {
        throw std::runtime_error("injected handler failure");
    });

    struct ThrowingFormatter : spdlog::formatter {
        void format(const spdlog::details::log_msg&, spdlog::memory_buf_t&) override
        {
            throw 42;
        }

        std::unique_ptr<spdlog::formatter> clone() const override
        {
            return std::make_unique<ThrowingFormatter>();
        }
    };

    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(std::cout);
    sink->set_formatter(std::make_unique<ThrowingFormatter>());
    destination->sinks().push_back(sink);
    Model::MetricsReporter reporter{destination};
    Model::FrameMetrics metrics;
    metrics.record(1ms, 0ns, false);
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!reporter.submit(metrics, {}) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    while (!reporter.failures() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    EXPECT_GE(reporter.failures(), 1U);
}

TEST(MetricsReporterTest, ReportsOwnedLifetimeStallCounts)
{
    std::ostringstream output;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(output);
    auto destination = std::make_shared<spdlog::logger>("stall-metrics", sink);
    Model::FrameMetrics metrics;
    metrics.record(51ms, 0ns, true);
    {
        Model::MetricsReporter reporter{destination};
        // Submission is deliberately best-effort and may contend briefly with
        // the newly started worker. Test snapshot ownership, not scheduling luck.
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        bool accepted = false;
        do {
            accepted = reporter.submit(metrics, {});
            if (!accepted)
                std::this_thread::yield();
        }
        while (!accepted && std::chrono::steady_clock::now() < deadline);
        ASSERT_TRUE(accepted);
        metrics.record(100ms, 0ns, true);
    }
    const auto text = output.str();
    EXPECT_NE(text.find("lifetime_launcher_gt_1ms=1"), std::string::npos);
    EXPECT_NE(text.find("lifetime_launcher_gt_5ms=1"), std::string::npos);
    EXPECT_NE(text.find("lifetime_launcher_gt_10ms=1"), std::string::npos);
    EXPECT_NE(text.find("lifetime_launcher_gt_50ms=1"), std::string::npos);
}
