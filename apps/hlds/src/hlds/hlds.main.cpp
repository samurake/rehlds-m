/*
 * Half Life 1 SDK License
 * Copyright(c) Valve Corp
 *
 * DISCLAIMER OF WARRANTIES. THE HALF LIFE 1 SDK AND ANY OTHER MATERIAL
 * DOWNLOADED BY LICENSEE IS PROVIDED "AS IS". VALVE AND ITS SUPPLIERS
 * DISCLAIM ALL WARRANTIES WITH RESPECT TO THE SDK, EITHER EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY,
 * NON-INFRINGEMENT, TITLE AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * LIMITATION OF LIABILITY. IN NO EVENT SHALL VALVE OR ITS SUPPLIERS BE LIABLE
 * FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER
 * (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
 * BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY OTHER PECUNIARY
 * LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE THE ENGINE AND/OR THE
 * SDK, EVEN IF VALVE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 *
 * For commercial use, contact: sourceengine@valvesoftware.com
 */

#include "common/engine/engine_wrapper.hpp"
#include "common/engine/interface/dedicated_serverapi_interface.hpp"
#include "core/cmdline_args.hpp"
#include "core/cmdline_processor.hpp"
#include "core/init.hpp"
#include "core/launcher_commands.hpp"
#include "model/console_commands.hpp"
#include "model/server_loop.hpp"
#include "model/userinput_history.hpp"
#include "presenter/input_presenter.hpp"
#include "presenter/output_presenter.hpp"
#include "util/logger.hpp"
#include "view/console_input.hpp"
#include "view/console_view.hpp"
#include <cstdio>
#include <memory>
#include <string_view>

namespace {
    std::string launcher_identity()
    {
        return std::string{"ReHLDS-M "} + REHLDSM_VERSION + " (" + REHLDSM_REVISION + ")";
    }

    struct LogSession {
        ~LogSession()
        {
            (void)Util::log_shutdown();
        }
    };

    // Engine ABI ownership remains unchanged; this guard controls shutdown order.
    class EngineSession final {
      public:
        explicit EngineSession(Common::DedicatedServerApiInterface& api) : api_(api)
        {
        }

        ~EngineSession()
        {
            api_.shutdown();
        }

      private:
        Common::DedicatedServerApiInterface& api_;
    };

    class Runtime final {
      public:
        Runtime(Common::DedicatedServerApiInterface& api, Common::SystemInterface* system, View::BaseView& view) :
          launcher_commands_(system, launcher_identity()),
          engine_(api),
          commands_(system),
          history_("input_history.txt"),
          input_presenter_(loop_, history_),
          output_presenter_(loop_, view),
          input_(input_presenter_)
        {
            if (!launcher_commands_.version_registered())
                loop_.set_launcher_version(launcher_identity());
        }

        ~Runtime()
        {
            loop_.close();
            input_.stop();
            // Members die in reverse order; EngineSession shuts down last.
        }

        int run(
          const Core::CmdLineArgs& args, const Core::StartupSettings& settings,
          Common::DedicatedServerApiInterface& api)
        {
            if (!Core::init_server_loop(args, settings, loop_))
                return 2;
            loop_.set_console_commands(commands_);
            loop_.prepare_reporting();
            loop_.run(api);
            return 0;
        }

      private:
        Core::LauncherCommands launcher_commands_; // outlives EngineSession
        EngineSession engine_;
        Model::ServerLoop loop_{};
        Model::ConsoleCommands commands_;
        Model::UserInputHistory history_;
        Presenter::InputPresenter input_presenter_;
        Presenter::OutputPresenter output_presenter_;
        View::ConsoleInput input_;
    };

    int run_server_loop(const Core::CmdLineArgs& args, const Core::StartupSettings& settings, View::BaseView& view)
    {
        auto& engine = Common::EngineWrapper::get_instance();
        auto& api = *engine.get_interface_or_abort<Common::EngineInterface::server_api>();
        const auto runtime = std::make_unique<Runtime>(api, engine.get_interface_system(), view);
        return runtime->run(args, settings, api);
    }
}

int main(const int argc, const char** const argv)
{
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::printf("ReHLDS-M %s (%s)\n", REHLDSM_VERSION, REHLDSM_REVISION);
        return 0;
    }
    auto cmdline_args = Core::CmdLineArgs::from_command_line(argc, argv);
    Core::StartupSettings settings;
    std::string error;
    if (!Core::load_startup_settings(cmdline_args, Core::startup_config_path(), settings, error)) {
        std::fprintf(stderr, "[ReHLDS-M] Startup configuration error: %s\n", error.c_str());
        return 2;
    }
    Core::apply_startup_settings(cmdline_args, settings);
    if (cmdline_args.contains("check-config")) {
        std::printf(
          "ReHLDS-M startup configuration valid: %s (%s)\n"
          "target_fps=%d [%s] frame_stats_seconds=%u [%s] async_output=%s [%s] "
          "output_queue_capacity=%u [%s] output_flush_ms=%u [%s]\n"
          "Loaded only at startup; restart to reload.\n",
          settings.config_path.string().c_str(), settings.config_found ? "loaded" : "missing", settings.target_fps,
          settings.sources[0].c_str(), settings.frame_stats_seconds, settings.sources[1].c_str(),
          settings.async_output ? "true" : "false", settings.sources[2].c_str(), settings.output_queue_capacity,
          settings.sources[3].c_str(), settings.output_flush_ms, settings.sources[4].c_str());
        return 0;
    }
    const auto console_view = std::make_shared<View::ConsoleView>();

    Core::init_logger(cmdline_args, console_view, settings);
    const LogSession log_session;
    const Core::CmdLineProcessor cmdline_processor{};
    cmdline_processor.process(cmdline_args);
    Util::log_info("{}\n", launcher_identity());
    Core::report_startup_settings(settings);

    Core::init_locale();
    Core::init_socket();
    Core::init_filesystem();
    Core::init_engine(cmdline_args);

    return run_server_loop(cmdline_args, settings, *console_view);
}
