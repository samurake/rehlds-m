#include "model/frame_metrics.hpp"
#include <gtest/gtest.h>
using namespace std::chrono_literals;

TEST(FrameMetricsTest, BoundsWindowAndRetainsLifetimeMaximum)
{
    Model::FrameMetrics metrics;
    metrics.record(10ms, 0ns, true);
    for (std::size_t i = 0; i < Model::FrameMetrics::CAPACITY; ++i) {
        metrics.engine_time(250us);
        metrics.record(1ms, 20us, false);
    }
    const auto s = metrics.summary();
    EXPECT_EQ(s.samples, Model::FrameMetrics::CAPACITY);
    EXPECT_EQ(s.frame_p99_us, 1000);
    EXPECT_EQ(s.engine_p99_us, 250);
    EXPECT_EQ(s.wake_p99_us, 20);
    EXPECT_EQ(s.max_frame_us, 10000);
    EXPECT_EQ(s.over_budget_frames, 1U);
    EXPECT_EQ(s.window_over_budget, 0U);
    EXPECT_EQ(s.window_max_frame_us, 1000);
}

TEST(FrameMetricsTest, SeparatesWorkAndOwnerThreadLauncherTimeUsingOwnedSnapshots)
{
    Model::FrameMetrics metrics;
    metrics.engine_time(250us);
    metrics.work_time(300us);
    metrics.record(1ms, 20us, true);
    const auto snapshot = metrics;
    metrics.engine_time(1us);
    metrics.work_time(2us);
    metrics.record(2ms, 0ns, false);
    const auto s = snapshot.summary();
    EXPECT_EQ(s.frames, 1U);
    EXPECT_EQ(s.work_p99_us, 300);
    EXPECT_EQ(s.launcher_work_p99_us, 50);
    EXPECT_EQ(s.window_over_budget, 1U);
    EXPECT_EQ(s.window_max_frame_us, 1000);
}

TEST(FrameMetricsTest, LifetimeStallsSurvivePercentileWindowRollover)
{
    Model::FrameMetrics metrics;
    metrics.record(51ms, 0ns, true);
    for (std::size_t i = 0; i < Model::FrameMetrics::CAPACITY; ++i)
        metrics.record(500us, 0ns, false);
    const auto s = metrics.summary();
    EXPECT_EQ(s.frame_p99_us, 500);
    EXPECT_EQ(s.window_max_frame_us, 500);
    EXPECT_EQ(s.lifetime_stalls, (std::array<std::uint64_t, 4>{1, 1, 1, 1}));
}

TEST(FrameMetricsTest, LifetimeStallThresholdsAreStrictAndSnapshotsOwnCounts)
{
    Model::FrameMetrics metrics;
    for (const auto threshold : Model::FrameMetrics::STALL_THRESHOLDS_NS) {
        metrics.record(std::chrono::nanoseconds{threshold}, 0ns, false);
        metrics.record(std::chrono::nanoseconds{threshold + 1}, 0ns, false);
    }
    const auto snapshot = metrics;
    EXPECT_EQ(snapshot.summary().lifetime_stalls, (std::array<std::uint64_t, 4>{7, 5, 3, 1}));
    metrics.record(100ms, 0ns, true);
    EXPECT_EQ(snapshot.summary().lifetime_stalls, (std::array<std::uint64_t, 4>{7, 5, 3, 1}));
    EXPECT_EQ(metrics.summary().lifetime_stalls, (std::array<std::uint64_t, 4>{8, 6, 4, 2}));
    EXPECT_EQ(Model::FrameMetrics{}.summary().lifetime_stalls, (std::array<std::uint64_t, 4>{}));
}
