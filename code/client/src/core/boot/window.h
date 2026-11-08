#pragma once

namespace KCDC::Core::Boot::Window {
    /**
     * Puts the mod's name on the game window and keeps it there.
     *
     * Must be called from `ApplyInitPatches()`. The game names the window at
     * `CreateWindowExW` and again through `SetWindowTextA`, both while the mod
     * is still scanning WHGame.dll for patterns, so the `MH_ALL_HOOKS` pass is
     * too late for either. These sit on `user32` exports and need no pattern.
     */
    void InstallTitlePatch();
} // namespace KCDC::Core::Boot::Window
