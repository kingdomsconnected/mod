#pragma once

#include <filesystem>

namespace KCDC::Launcher {
    // Bring the installation up to date with the MafiaHub build channel it is
    // stamped with; a no-op returning true when KCDC_PATCHER_ENABLED is off,
    // or when .mafiahub/distribution hands updates to another storefront.
    // Returns false when the updater is taking over and will relaunch us, so
    // the caller must exit without starting the game. A failed patch only
    // warns.
    bool RunBuildPatcher(const std::filesystem::path &launcherExecutable);
} // namespace KCDC::Launcher
