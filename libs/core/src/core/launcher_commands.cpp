#include "core/launcher_commands.hpp"
#include "util/logger.hpp"

namespace Core {
    LauncherCommands::LauncherCommands(Common::SystemInterface* system, std::string identity) :
      system_(system),
      identity_(std::move(identity))
    {
        if (system_)
            registered_ = system_->register_command("rehldsm_version", this, 1);
        if (!registered_)
            Util::log_warn("rehldsm_version engine registration unavailable; launcher console fallback only.");
    }

    void LauncherCommands::execute_command(int id, const char*)
    {
        // Route through engine printing while the RCON redirect is still active.
        // The launcher output callback can queue its separate console copy safely.
        if (id == 1 && system_)
            system_->print("%s\n", identity_.c_str());
    }
}
