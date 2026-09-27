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
#include "model/console_commands.hpp"
#include "model/server_loop.hpp"
#include "model/userinput_history.hpp"
#include "presenter/input_presenter.hpp"
#include "presenter/output_presenter.hpp"
#include "view/console_input.hpp"
#include "view/console_view.hpp"
#include "util/logger.hpp"
#include <cstdio>
#include <memory>
#include <string_view>

namespace {
    // Engine ABI ownership remains unchanged; this guard controls shutdown order.
    class EngineSession final {
      public:
        explicit EngineSession(Common::DedicatedServerApiInterface& api) : api_(api) {}
        ~EngineSession() { api_.shutdown(); }
      private:
        Common::DedicatedServerApiInterface& api_;
    };
    class Runtime final {
      public:
        Runtime(Common::DedicatedServerApiInterface& api, Common::SystemInterface* system, View::BaseView& view)
          : engine_(api), commands_(system), history_("input_history.txt"),
            input_presenter_(loop_, history_), output_presenter_(loop_, view), input_(input_presenter_) {}
        ~Runtime()
        {
            loop_.close();
            input_.stop();
            // Members die in reverse order; EngineSession shuts down last.
        }
        int run(const Core::CmdLineArgs& args, Common::DedicatedServerApiInterface& api)
        {
            if (!Core::init_server_loop(args, loop_)) return 2;
            loop_.set_console_commands(commands_);
            loop_.run(api);
            return 0;
        }
      private:
        EngineSession engine_;
        Model::ServerLoop loop_{};
        Model::ConsoleCommands commands_;
        Model::UserInputHistory history_;
        Presenter::InputPresenter input_presenter_;
        Presenter::OutputPresenter output_presenter_;
        View::ConsoleInput input_;
    };
    int run_server_loop(const Core::CmdLineArgs& args, View::BaseView& view)
    {
        auto& engine = Common::EngineWrapper::get_instance();
        auto& api = *engine.get_interface_or_abort<Common::EngineInterface::server_api>();
        const auto runtime = std::make_unique<Runtime>(api, engine.get_interface_system(), view);
        return runtime->run(args, api);
    }
}

int main(const int argc, const char** const argv)
{
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::printf("ReHLDS-M %s (%s)\n", REHLDSM_VERSION, REHLDSM_REVISION);
        return 0;
    }
    auto cmdline_args = Core::CmdLineArgs::from_command_line(argc, argv);
    const auto console_view = std::make_shared<View::ConsoleView>();

    Core::init_logger(cmdline_args, console_view);
    Util::log_info("ReHLDS-M {} ({})\n", REHLDSM_VERSION, REHLDSM_REVISION);
    const Core::CmdLineProcessor cmdline_processor{};
    cmdline_processor.process(cmdline_args);

    Core::init_locale();
    Core::init_socket();
    Core::init_filesystem();
    Core::init_engine(cmdline_args);

    return run_server_loop(cmdline_args, *console_view);
}
