#include "util/logger.hpp"
#include "util/string.hpp"
#include <gtest/gtest.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std::chrono_literals;

namespace {
    struct Capture : Util::LogOutput {
        std::string text;

        void write_log(std::string_view message) override
        {
            text.append(message);
        }
    };
}

TEST(LoggerTest, PreservesBracesAndBoundsFormattedOutput)
{
    auto capture = std::make_shared<Capture>();
    Util::LogSettings settings{};
    settings.log_output = capture;
    Util::log_init(settings);
    Util::log_info("{}", "literal {name} {} }");
    EXPECT_EQ(capture->text, "literal {name} {} }");
    capture->text.clear();
    Util::log_info("{}", std::string(20000, 'x'));
    EXPECT_LT(capture->text.size(), 8300U);
    EXPECT_NE(capture->text.find("truncated"), std::string::npos);
}

TEST(LoggerTest, OptionalFileFailureKeepsConsoleLogging)
{
    auto capture = std::make_shared<Capture>();
    Util::LogSettings settings{};
    settings.log_output = capture;
    settings.output = Util::LogDestination::view_and_file;
    settings.logfile = "/rehldsm-nonexistent-parent/log.txt";
    Util::log_init(settings);
    Util::log_warn("still running");
    EXPECT_EQ(capture->text, "[WARNING] still running\n");
}
#ifdef __linux__
TEST(LoggerTest, FullFilesystemDisablesOptionalSink)
{
    auto capture = std::make_shared<Capture>();
    Util::LogSettings settings{};
    settings.log_output = capture;
    settings.output = Util::LogDestination::view_and_file;
    settings.logfile = "/dev/full";
    Util::log_init(settings);
    Util::log_warn("first");
    Util::log_warn("second");
    EXPECT_NE(capture->text.find("second"), std::string::npos);
}
#endif
TEST(StringConversionTest, RejectsPartiallyParsedAndOverflowedIntegers)
{
    EXPECT_FALSE(Util::str::convert_to_type<int>("1000garbage"));
    EXPECT_FALSE(Util::str::convert_to_type<int>("999999999999999999999"));
    EXPECT_EQ(Util::str::convert_to_type<int>("1000"), 1000);
}

TEST(FormattingTest, LongDoubleHexAndOrdinaryNumericFormattingRemainSupported)
{
    // MSVC long double is 64-bit; Linux i386 uses the 80-bit fallback path.
    for (const auto value : {1.5L, 1.53125L}) {
        const auto hex = fmt::format("{:.2a}", value);
        EXPECT_EQ(hex.substr(0, 2), "0x");
        EXPECT_EQ(std::strtold(hex.c_str(), nullptr), value);
    }
    EXPECT_EQ(fmt::format("{:.3f}", 1.5L), "1.500");
    EXPECT_EQ(fmt::format("{} {:.1f}", 2000, 1992.25), "2000 1992.2");
}

TEST(LoggerTest, AsyncOutputOwnsPayloadPreservesAcceptedOrderAndFlushesWithoutNewMessages)
{
    auto capture = std::make_shared<Capture>();
    Util::LogSettings settings{};
    settings.log_output = capture;
    settings.asynchronous = true;
    Util::log_init(settings);
    std::string input = "first {literal}\n";
    Util::log_info("{}", input);
    input.assign("changed");
    ASSERT_TRUE(Util::log_flush(1s));
    Util::log_warn("second");
    ASSERT_TRUE(Util::log_flush(1s));
    EXPECT_EQ(capture->text, "first {literal}\n[WARNING] second\n");
    Util::log_info("{}", std::string(20000, 'x'));
    ASSERT_TRUE(Util::log_flush(1s));
    EXPECT_NE(capture->text.find("truncated"), std::string::npos);
    EXPECT_EQ(Util::log_output_stats().dropped, 0U);
    EXPECT_TRUE(Util::log_shutdown());
}

TEST(LoggerTest, AsyncTimerFlushesQuietOutput)
{
    struct TimedCapture : Util::LogOutput {
        std::mutex mutex;
        std::condition_variable ready;
        bool written{};

        void write_log(std::string_view) override
        {
            {
                const std::scoped_lock lock{mutex};
                written = true;
            }
            ready.notify_one();
        }
    };

    auto capture = std::make_shared<TimedCapture>();
    Util::LogSettings settings{};
    settings.log_output = capture;
    settings.asynchronous = true;
    settings.flush_interval_ms = 10;
    Util::log_init(settings);
    Util::log_info("quiet output\n");
    std::unique_lock lock{capture->mutex};
    EXPECT_TRUE(capture->ready.wait_for(lock, 1s, [&] {
        return capture->written;
    }));
    lock.unlock();
    EXPECT_TRUE(Util::log_shutdown());
}

TEST(LoggerTest, SaturatedSlowOutputDoesNotBlockProducerAndShutdownOwnsDetachedState)
{
    struct BlockedCapture : Util::LogOutput {
        std::mutex mutex;
        std::condition_variable ready;
        bool entered{}, release{}, finished{};

        void write_log(std::string_view) override
        {
            std::unique_lock lock{mutex};
            entered = true;
            ready.notify_all();
            ready.wait(lock, [&] {
                return release;
            });
            finished = true;
            ready.notify_all();
        }
    };

    auto capture = std::make_shared<BlockedCapture>();
    Util::LogSettings settings{};
    settings.log_output = capture;
    settings.asynchronous = true;
    settings.queue_capacity = 64;
    Util::log_init(settings);
    Util::log_warn("block worker");
    std::unique_lock lock{capture->mutex};
    ASSERT_TRUE(capture->ready.wait_for(lock, 1s, [&] {
        return capture->entered;
    }));
    lock.unlock();
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 10000; ++i)
        Util::log_info("message {}\n", i);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 500ms);
    EXPECT_EQ(Util::log_output_stats().high_water, 56U); // Eight slots reserved for warnings/errors.
    EXPECT_GE(Util::log_output_stats().dropped, 9936U);
    EXPECT_FALSE(Util::log_shutdown(20ms));
    {
        const std::scoped_lock guard{capture->mutex};
        capture->release = true;
    }
    capture->ready.notify_all();
    lock.lock();
    EXPECT_TRUE(capture->ready.wait_for(lock, 1s, [&] {
        return capture->finished;
    }));
}

TEST(LoggerTest, AsyncOutputFailureIsVisibleAndDoesNotAbort)
{
    struct FailingCapture : Util::LogOutput {
        void write_log(std::string_view) override
        {
        }

        bool try_write_log(std::string_view) override
        {
            return false;
        }
    };

    Util::LogSettings settings{};
    settings.log_output = std::make_shared<FailingCapture>();
    settings.asynchronous = true;
    Util::log_init(settings);
    Util::log_warn("continue");
    ASSERT_TRUE(Util::log_flush(1s));
    EXPECT_GE(Util::log_output_stats().write_failures, 1U);
    EXPECT_TRUE(Util::log_shutdown());
}

namespace {
    struct ThrowingSink : spdlog::sinks::base_sink<std::mutex> {
        explicit ThrowingSink(bool flush) : on_flush(flush)
        {
        }

        bool on_flush;
        unsigned calls{};

        void sink_it_(const spdlog::details::log_msg&) override
        {
            if (!on_flush) {
                ++calls;
                throw std::runtime_error("injected log failure");
            }
        }

        void flush_() override
        {
            if (on_flush) {
                ++calls;
                throw 42;
            } // Also contain non-standard exceptions.
        }
    };

    struct HealthySink : spdlog::sinks::base_sink<std::mutex> {
        std::string text;

        void sink_it_(const spdlog::details::log_msg& message) override
        {
            text.append(message.payload.data(), message.payload.size());
        }

        void flush_() override
        {
        }
    };
}

TEST(LoggerTest, ThrowingDestinationIsDisabledAndHealthyDestinationContinues)
{
    for (const bool on_flush : {false, true}) {
        auto failing = std::make_shared<ThrowingSink>(on_flush);
        auto healthy = std::make_shared<HealthySink>();
        Util::AsyncLogSink sink{
          {failing, healthy},
          64, 1000
        };
        sink.set_formatter(std::make_unique<spdlog::pattern_formatter>());
        sink.log({"main", spdlog::level::info, "first\n"});
        ASSERT_TRUE(sink.flush_and_wait(1s));
        sink.log({"main", spdlog::level::warn, "second\n"});
        ASSERT_TRUE(sink.flush_and_wait(1s));
        EXPECT_EQ(sink.stats().write_failures, 1U);
        EXPECT_TRUE(sink.stop(1s));
        EXPECT_EQ(failing->calls, 1U);
        EXPECT_EQ(healthy->text, "first\nsecond\n");
    }
}

TEST(LoggerTest, SaturatedInfoQueueReservesWarningsWithoutReorderingAcceptedMessages)
{
    // No consumer yet: deterministically fill the normal and reserved capacity.
    auto healthy = std::make_shared<HealthySink>();
    Util::AsyncLogSink sink{{healthy}, 64, 1000};
    for (unsigned i = 0; i < 56; ++i)
        sink.log({"main", spdlog::level::info, "i"});
    sink.log({"main", spdlog::level::info, "discarded"});
    for (unsigned i = 0; i < 8; ++i)
        sink.log({"main", spdlog::level::warn, "w"});
    sink.log({"main", spdlog::level::err, "also discarded"});
    EXPECT_EQ(sink.stats().high_water, 64U);
    EXPECT_EQ(sink.stats().dropped, 2U);
    sink.set_formatter(std::make_unique<spdlog::pattern_formatter>());
    ASSERT_TRUE(sink.flush_and_wait(1s));
    ASSERT_TRUE(sink.stop(1s));
    EXPECT_EQ(healthy->text, std::string(56, 'i') + std::string(8, 'w'));
}

TEST(LoggerTest, AsyncConcurrentProducersKeepEveryMessageAndPerProducerOrder)
{
    auto capture = std::make_shared<Capture>();
    Util::LogSettings settings{};
    settings.log_output = capture;
    settings.asynchronous = true;
    settings.queue_capacity = 4096;
    Util::log_init(settings);
    {
        std::vector<std::jthread> producers;
        for (unsigned producer = 0; producer < 4; ++producer)
            producers.emplace_back([producer] {
                for (unsigned sequence = 0; sequence < 256; ++sequence)
                    Util::log_info("{}:{}\n", producer, sequence);
            });
    }
    ASSERT_TRUE(Util::log_flush(2s));
    EXPECT_EQ(Util::log_output_stats().dropped, 0U);
    std::array<unsigned, 4> next{};
    std::istringstream input{capture->text};
    std::string line;
    while (std::getline(input, line)) {
        unsigned producer{}, sequence{};
        char colon{};
        std::istringstream record{line};
        ASSERT_TRUE(static_cast<bool>(record >> producer >> colon >> sequence));
        ASSERT_LT(producer, next.size());
        EXPECT_EQ(colon, ':');
        EXPECT_EQ(sequence, next[producer]++);
    }
    for (const auto count : next)
        EXPECT_EQ(count, 256U);
    EXPECT_TRUE(Util::log_shutdown());
}
