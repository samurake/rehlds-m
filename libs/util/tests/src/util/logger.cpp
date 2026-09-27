#include "util/logger.hpp"
#include "util/string.hpp"
#include <gtest/gtest.h>

namespace {
    struct Capture : Util::LogOutput {
        std::string text;
        void write_log(std::string_view message) override { text.append(message); }
    };
}
TEST(LoggerTest, PreservesBracesAndBoundsFormattedOutput)
{
    auto capture = std::make_shared<Capture>();
    Util::LogSettings settings{}; settings.log_output = capture;
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
    Util::LogSettings settings{}; settings.log_output = capture;
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
    Util::LogSettings settings{}; settings.log_output = capture;
    settings.output = Util::LogDestination::view_and_file; settings.logfile = "/dev/full";
    Util::log_init(settings);
    Util::log_warn("first"); Util::log_warn("second");
    EXPECT_NE(capture->text.find("second"), std::string::npos);
}
#endif
TEST(StringConversionTest, RejectsPartiallyParsedAndOverflowedIntegers)
{
    EXPECT_FALSE(Util::str::convert_to_type<int>("1000garbage"));
    EXPECT_FALSE(Util::str::convert_to_type<int>("999999999999999999999"));
    EXPECT_EQ(Util::str::convert_to_type<int>("1000"), 1000);
}
