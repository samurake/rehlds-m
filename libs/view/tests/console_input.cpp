#include "view/console_input.hpp"
#include "common/engine/interface/dedicated_serverapi_interface.hpp"
#include "presenter/input_presenter.hpp"
#include "util/logger.hpp"
#include "view/linux/console_view.hpp"
#include <gtest/gtest.h>
#include <ctime>
#include <unistd.h>
using namespace std::chrono_literals;

namespace {
    struct StdinPipe {
        int saved = ::dup(STDIN_FILENO), fd[2]{-1, -1};

        StdinPipe()
        {
            if (::pipe(fd) != 0 || ::dup2(fd[0], STDIN_FILENO) < 0)
                std::abort();
            ::close(fd[0]);
        }

        ~StdinPipe()
        {
            ::dup2(saved, STDIN_FILENO);
            ::close(saved);
            if (fd[1] >= 0)
                ::close(fd[1]);
        }

        void eof()
        {
            ::close(fd[1]);
            fd[1] = -1;
        }

        void write(std::string_view text)
        {
            ASSERT_EQ(::write(fd[1], text.data(), text.size()), static_cast<ssize_t>(text.size()));
        }
    };

    struct Discard : Util::LogOutput {
        void write_log(std::string_view) override
        {
        }
    };

    class InputTest : public ::testing::Test {
      protected:
        void SetUp() override
        {
            Util::LogSettings settings;
            settings.log_output = std::make_shared<Discard>();
            Util::log_init(settings);
        }

        Model::ServerLoop loop;
        Model::UserInputHistory history;
        Presenter::InputPresenter presenter{loop, history};
    };
}

TEST_F(InputTest, EofDoesNotSpin)
{
    StdinPipe pipe;
    View::ConsoleInput input{presenter};
    pipe.eof();
    const auto cpu = std::clock();
    std::this_thread::sleep_for(300ms);
    input.stop();
    EXPECT_LT(static_cast<double>(std::clock() - cpu) / CLOCKS_PER_SEC, 0.10);
}

TEST_F(InputTest, PartialEscapeAndRepeatedStopJoinPromptly)
{
    StdinPipe pipe;
    View::ConsoleInput input{presenter};
    pipe.write("\x1B[");
    std::this_thread::sleep_for(30ms);
    const auto begin = std::chrono::steady_clock::now();
    input.stop();
    input.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 500ms);
}

TEST_F(InputTest, CompletionCanBeStoppedWithoutAnEngineLoop)
{
    StdinPipe pipe;
    View::ConsoleInput input{presenter};
    pipe.write("stat\t");
    std::this_thread::sleep_for(30ms);
    const auto begin = std::chrono::steady_clock::now();
    input.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, 500ms);
}

TEST_F(InputTest, OversizedInputIsRejectedAsAWholeLine)
{
    StdinPipe pipe;
    View::ConsoleInput input{presenter};
    pipe.write(std::string(1500, 'x') + "\nvalid\n");
    std::this_thread::sleep_for(50ms);
    input.stop();
    EXPECT_EQ(history.previous(), "valid");
    EXPECT_FALSE(history.previous());
    EXPECT_EQ(loop.input_high_watermark(), 1U);
}

TEST(ConsoleViewTest, PrintsBracesLiterally)
{
    testing::internal::CaptureStdout();
    View::ConsoleView view;
    EXPECT_EQ(view.print("literal {name} {} }\n"), 0);
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "literal {name} {} }\n");
}

TEST(ConsoleViewTest, AsyncPipeBackpressureIsBoundedWithoutChangingStdoutFlags)
{
    int pipe[2];
    ASSERT_EQ(::pipe(pipe), 0);
    const auto saved = ::dup(STDOUT_FILENO);
    ASSERT_GE(saved, 0);
    ASSERT_GE(::dup2(pipe[1], STDOUT_FILENO), 0);
    ::close(pipe[1]);
    const auto flags = ::fcntl(STDOUT_FILENO, F_GETFL);
    {
        View::ConsoleView view;
        const auto start = std::chrono::steady_clock::now();
        const auto written = view.try_write_log(std::string(256 * 1024, 'x'));
        const auto elapsed = std::chrono::steady_clock::now() - start;
        const auto after = ::fcntl(STDOUT_FILENO, F_GETFL);
        ::dup2(saved, STDOUT_FILENO);
        ::close(saved);
        EXPECT_FALSE(written);
        EXPECT_LT(elapsed, 500ms);
        EXPECT_EQ(flags, after);
    }
    ::close(pipe[0]);
}
