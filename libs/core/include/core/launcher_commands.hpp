#pragma once

#include "common/engine/interface/system_interface.hpp"
#include <string>

namespace Core {
    // Engine callbacks execute on the engine owner thread. Keep this object alive
    // through engine shutdown, when the engine removes its wrapper commands.
    class LauncherCommands final : public Common::SystemModuleInterface {
      public:
        LauncherCommands(Common::SystemInterface* system, std::string identity);
        LauncherCommands(const LauncherCommands&) = delete;
        LauncherCommands& operator=(const LauncherCommands&) = delete;

        bool version_registered() const
        {
            return registered_;
        }

        bool init(Common::SystemInterface*, int, const char*) override
        {
            return true;
        }

        void run_frame(double) override
        {
        }

        void receive_signal(Common::SystemModuleInterface*, unsigned, void*) override
        {
        }

        FORCE_STACK_ALIGN void execute_command(int id, const char*) override;

        void register_listener(Common::SystemModuleInterface*) override
        {
        }

        void remove_listener(Common::SystemModuleInterface*) override
        {
        }

        Common::SystemInterface* get_system() override
        {
            return system_;
        }

        int get_serial() override
        {
            return 0x524d;
        }

        char* get_status_line() override
        {
            return name_;
        }

        char* get_type() override
        {
            return name_;
        }

        char* get_name() override
        {
            return name_;
        }

        int get_state() override
        {
            return 1;
        }

        int get_version() override
        {
            return 1;
        }

        void shutdown() override
        {
        }

      private:
        Common::SystemInterface* system_{};
        std::string identity_;
        char name_[9]{"ReHLDS-M"};
        bool registered_{};
    };
}
