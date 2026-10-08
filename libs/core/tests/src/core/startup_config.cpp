#include "core/startup_config.hpp"
#include "core/cmdline_processor.hpp"
#include "core/init.hpp"
#include "util/logger.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>

namespace {
    class StartupConfigTest : public testing::Test {
      protected:
        std::filesystem::path directory =
          std::filesystem::temp_directory_path() /
          ("rehldsm-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::path path = directory / "rehldsm.cfg";
        Core::StartupSettings settings{};
        std::string error{};

        void SetUp() override
        {
            std::filesystem::create_directory(directory);
        }

        void TearDown() override
        {
            std::filesystem::remove_all(directory);
        }

        void write(std::string_view text)
        {
            std::ofstream file{path, std::ios::binary};
            file << text;
        }

        Core::CmdLineArgs args()
        {
            const char* argv[]{"hlds", "-framestats", "5", "-pingboost", "2", "-game", "cstrike"};
            return Core::CmdLineArgs::from_command_line(7, argv);
        }
    };
}

TEST_F(StartupConfigTest, MissingFileRetainsCliAndDefaults)
{
    ASSERT_TRUE(Core::load_startup_settings(args(), path, settings, error)) << error;
    EXPECT_FALSE(settings.config_found);
    EXPECT_EQ(settings.target_fps, 0);
    EXPECT_EQ(settings.frame_stats_seconds, 5U);
    EXPECT_FALSE(settings.async_output);
    EXPECT_EQ(settings.output_queue_capacity, 1024U);
    EXPECT_EQ(settings.sources[0], "default");
    EXPECT_EQ(settings.sources[1], "command line");
}

TEST_F(StartupConfigTest, ExplicitConfigOverridesCliWithoutChangingEngineArguments)
{
    write("target_fps = 1500\nasync_output = true\noutput_queue_capacity = 64\noutput_flush_ms = 10\n");
    auto cli = args();
    ASSERT_TRUE(Core::load_startup_settings(cli, path, settings, error)) << error;
    Core::apply_startup_settings(cli, settings);
    Core::CmdLineProcessor{}.process(cli);
    EXPECT_FALSE(cli.contains("targetfps"));
    EXPECT_FALSE(cli.contains("framestats"));
    Model::ServerLoop loop;
    ASSERT_TRUE(Core::init_server_loop(cli, settings, loop));
    EXPECT_EQ(loop.get_target_fps(), 1500);
    EXPECT_EQ(loop.get_pingboost_level(), Model::PingBoostLevel::target_fps);
    EXPECT_EQ(cli.get_argument_option_as<int>("sys_ticrate"), 0);
    EXPECT_EQ(cli.get_argument_option("game"), "cstrike");
    EXPECT_FALSE(cli.contains("pingboost"));
    EXPECT_TRUE(settings.async_output);
}

TEST_F(StartupConfigTest, ZeroValuesDisablePacingAndPeriodicReports)
{
    write("target_fps=0\nframe_stats_seconds=0\n");
    auto cli = args();
    ASSERT_TRUE(Core::load_startup_settings(cli, path, settings, error));
    Core::apply_startup_settings(cli, settings);
    Core::CmdLineProcessor{}.process(cli);
    EXPECT_FALSE(cli.contains("targetfps"));
    EXPECT_FALSE(cli.contains("framestats"));
    EXPECT_EQ(cli.get_argument_option_as<int>("pingboost"), 2);
}

TEST_F(StartupConfigTest, SupportsBomCrlfWhitespaceAndInlineComments)
{
    write("\xEF\xBB\xBF# header\r\n \ttarget_fps = 2000 # pacing\r\nasync_output = false\r\n");
    ASSERT_TRUE(Core::load_startup_settings(args(), path, settings, error)) << error;
    EXPECT_EQ(settings.target_fps, 2000);
    EXPECT_FALSE(settings.async_output);
}

TEST_F(StartupConfigTest, RejectsUnknownDuplicateMalformedAndEmptySettings)
{
    for (const auto* text :
         {"target_fps=1500\nunknown=true", "target_fps=1\ntarget_fps=2", "target_fps 1500",
          "target_fps=", "async_output=1"}) {
        write(text);
        EXPECT_FALSE(Core::load_startup_settings(args(), path, settings, error)) << text;
        EXPECT_FALSE(error.empty());
    }
}

TEST_F(StartupConfigTest, RejectsOverflowPartialNegativeAndOutOfRangeNumbers)
{
    for (const auto* text :
         {"target_fps=2001", "target_fps=10000", "target_fps=-1", "target_fps=1500junk",
          "target_fps=999999999999999999999", "frame_stats_seconds=3601", "output_queue_capacity=63",
          "output_queue_capacity=4097", "output_flush_ms=0", "output_flush_ms=1", "output_flush_ms=9",
          "output_flush_ms=1001"}) {
        write(text);
        EXPECT_FALSE(Core::load_startup_settings(args(), path, settings, error)) << text;
    }
}

TEST_F(StartupConfigTest, RejectsUnreadableOversizedAndNulFiles)
{
    write(std::string(65537, '#'));
    EXPECT_FALSE(Core::load_startup_settings(args(), path, settings, error));
    write(std::string_view{"target_fps=1500\0", 16});
    EXPECT_FALSE(Core::load_startup_settings(args(), path, settings, error));
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    EXPECT_FALSE(Core::load_startup_settings(args(), path, settings, error));
}

TEST_F(StartupConfigTest, ValuesRemainUnchangedAfterFileEditUntilNextLoad)
{
    write("target_fps=1500");
    ASSERT_TRUE(Core::load_startup_settings(args(), path, settings, error));
    write("target_fps=2000");
    EXPECT_EQ(settings.target_fps, 1500);
    ASSERT_TRUE(Core::load_startup_settings(args(), path, settings, error));
    EXPECT_EQ(settings.target_fps, 2000);
}

TEST_F(StartupConfigTest, RejectsRemovedTargetFpsFlagEvenWithValidConfig)
{
    for (const auto* value : {"1500", "2001", "broken"}) {
        const char* argv[]{"hlds", "-targetfps", value};
        const auto cli = Core::CmdLineArgs::from_command_line(3, argv);
        EXPECT_FALSE(Core::load_startup_settings(cli, path, settings, error));
        EXPECT_NE(error.find("has been removed"), error.npos);
        write("target_fps=1500");
        EXPECT_FALSE(Core::load_startup_settings(cli, path, settings, error));
    }
}

TEST_F(StartupConfigTest, AcceptsExactSafetyBoundaries)
{
    write("target_fps=2000\nframe_stats_seconds=3600\noutput_queue_capacity=4096\noutput_flush_ms=1000\n");
    ASSERT_TRUE(Core::load_startup_settings(args(), path, settings, error)) << error;
    EXPECT_EQ(settings.target_fps, Model::ServerLoop::MAX_TARGET_FPS);
    write("target_fps=0\nframe_stats_seconds=0\noutput_queue_capacity=64\noutput_flush_ms=10\n");
    ASSERT_TRUE(Core::load_startup_settings(args(), path, settings, error)) << error;
}

TEST_F(StartupConfigTest, KeepsSupportedPingboostLevelsAndRejectsInvalidLegacyModes)
{
    for (const auto* value : {"0", "1", "2", "3", "4", "5"}) {
        const char* argv[]{"hlds", "-pingboost", value};
        auto cli = Core::CmdLineArgs::from_command_line(3, argv);
        ASSERT_TRUE(Core::load_startup_settings(cli, path, settings, error)) << error;
        Core::apply_startup_settings(cli, settings);
        Model::ServerLoop loop;
        ASSERT_TRUE(Core::init_server_loop(cli, settings, loop));
        EXPECT_EQ(static_cast<int>(loop.get_pingboost_level()), std::stoi(value));
    }
    for (const auto* value : {"-1", "6", "999", "broken", ""}) {
        const char* argv[]{"hlds", "-pingboost", value};
        EXPECT_FALSE(Core::load_startup_settings(Core::CmdLineArgs::from_command_line(3, argv), path, settings, error));
    }
}

TEST_F(StartupConfigTest, ReportConfigStillOverridesLegacyReportFlag)
{
    const char* argv[]{"hlds", "-framestats", "broken"};
    const auto cli = Core::CmdLineArgs::from_command_line(3, argv);
    EXPECT_FALSE(Core::load_startup_settings(cli, path, settings, error));
    write("frame_stats_seconds=1");
    ASSERT_TRUE(Core::load_startup_settings(cli, path, settings, error));
    EXPECT_EQ(settings.frame_stats_seconds, 1U);
}

TEST_F(StartupConfigTest, TypedLoopInitializationCannotBypassTargetCeiling)
{
    struct Capture : Util::LogOutput {
        std::string text;

        void write_log(std::string_view message) override
        {
            text.append(message);
        }
    };

    auto capture = std::make_shared<Capture>();
    Util::LogSettings logging;
    logging.log_output = capture;
    Util::log_init(logging);
    Model::ServerLoop loop;
    settings.target_fps = 2001;
    EXPECT_FALSE(Core::init_server_loop(args(), settings, loop));
    EXPECT_NE(capture->text.find("between 0 and 2000"), std::string::npos);
    EXPECT_TRUE(Util::log_shutdown());
}
