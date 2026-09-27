#include "model/server_loop.hpp"
#include "common/engine/interface/dedicated_serverapi_interface.hpp"
#include <gtest/gtest.h>
#include <thread>
using namespace std::chrono_literals;

namespace {
    class FakeEngine final : public Common::DedicatedServerApiInterface {
      public:
        bool init(const char*, const char*, Common::CreateInterfaceFunc, Common::CreateInterfaceFunc) override { return true; }
        int shutdown() override { return 0; }
        bool run_frame() override { return ++calls < 4; }
        void add_console_text(const char* text) override { inputs.emplace_back(text); }
        void update_status(float* fps, int*, int*, char* map) override { *fps = 500; map[0] = '\0'; }
        int calls{};
        std::vector<std::string> inputs;
    };
}
TEST(FramePacerTest, UsesAbsoluteDeadlinesWithoutOversleepDrift)
{
    Model::FramePacer pacer{Model::FrameClock::time_point{}, 1ms};
    EXPECT_EQ(pacer.deadline().time_since_epoch(), 1ms);
    pacer.advance(Model::FrameClock::time_point{1050us});
    EXPECT_EQ(pacer.deadline().time_since_epoch(), 2ms);
    pacer.advance(Model::FrameClock::time_point{2050us});
    EXPECT_EQ(pacer.deadline().time_since_epoch(), 3ms);
}
TEST(FramePacerTest, SkipsMissedDeadlinesWithoutCatchupLoop)
{
    Model::FramePacer pacer{Model::FrameClock::time_point{}, 1ms};
    pacer.advance(Model::FrameClock::time_point{10s});
    EXPECT_EQ(pacer.deadline().time_since_epoch(), 10001ms);
}
TEST(FrameMetricsTest, BoundsWindowAndRetainsLifetimeMaximum)
{
    Model::FrameMetrics metrics;
    metrics.record(10ms, 0ns, true);
    for (std::size_t i = 0; i < Model::FrameMetrics::CAPACITY; ++i) {
        metrics.engine_time(250us); metrics.record(1ms, 20us, false);
    }
    const auto s = metrics.summary();
    EXPECT_EQ(s.samples, Model::FrameMetrics::CAPACITY);
    EXPECT_EQ(s.frame_p99_us, 1000); EXPECT_EQ(s.engine_p99_us, 250);
    EXPECT_EQ(s.wake_p99_us, 20); EXPECT_EQ(s.max_frame_us, 10000);
    EXPECT_EQ(s.over_budget_frames, 1U);
}
TEST(ServerLoopTest, RejectsOversizedOrInjectedLines)
{
    Model::ServerLoop loop;
    EXPECT_FALSE(loop.enqueue_input(std::string(1023, 'x')));
    EXPECT_FALSE(loop.enqueue_input("one\ntwo"));
    EXPECT_FALSE(loop.enqueue_input(std::string_view{"a\0b", 3}));
    EXPECT_TRUE(loop.enqueue_input(std::string(1022, 'x')));
}
TEST(ServerLoopTest, QuitBypassesSaturatedInputQueue)
{
    Model::ServerLoop loop; FakeEngine engine;
    for (int i = 0; i < 128; ++i) ASSERT_TRUE(loop.enqueue_input("status"));
    EXPECT_FALSE(loop.enqueue_input("extra"));
    ASSERT_TRUE(loop.enqueue_input(" QUIT "));
    loop.run(engine, 1000);
    ASSERT_FALSE(engine.inputs.empty()); EXPECT_EQ(engine.inputs.front(), "quit\n");
    EXPECT_EQ(loop.metrics().summary().frames, 3U);
    EXPECT_FALSE(loop.enqueue_input("after shutdown"));
}
TEST(ServerLoopTest, CloseCancelsPendingCompletions)
{
    Model::ServerLoop loop;
    auto request = loop.request_completion("stat"); ASSERT_TRUE(request);
    loop.close();
    EXPECT_TRUE(request->cancelled());
    EXPECT_TRUE(request->wait({}, 1s).empty());
    EXPECT_FALSE(loop.request_completion("new"));
}
TEST(ServerLoopTest, CompletionQueueIsBoundedAndOwnsQuery)
{
    Model::ServerLoop loop;
    std::string query = "status";
    auto first = loop.request_completion(query); query.assign("changed");
    EXPECT_EQ(first->query(), "status");
    for (int i = 1; i < 16; ++i) ASSERT_TRUE(loop.request_completion("x"));
    EXPECT_FALSE(loop.request_completion("overflow")); loop.close();
}
TEST(CompletionRequestTest, StopTokenWakesWaiterAndRejectsLateResult)
{
    Model::CompletionRequest request{"status"};
    std::stop_source stop;
    std::jthread worker{[&] { std::this_thread::sleep_for(10ms); stop.request_stop(); }};
    const auto begin = std::chrono::steady_clock::now();
    EXPECT_TRUE(request.wait(stop.get_token(), 5s).empty());
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 1s);
    request.complete({"status"}); EXPECT_TRUE(request.wait({}, 0ms).empty());
}
TEST(CompletionRequestTest, TimeoutCancelsAndCompletedRequestReturnsResults)
{
    Model::CompletionRequest timeout{"a"}; EXPECT_TRUE(timeout.wait({}, 1ms).empty());
    EXPECT_TRUE(timeout.cancelled());
    Model::CompletionRequest ready{"b"}; ready.complete({"beta"});
    EXPECT_EQ(ready.wait({}, 1ms), std::vector<std::string>{"beta"});
}
