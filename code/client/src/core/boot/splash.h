#pragma once

namespace KCDC::Core::Boot::Splash {
    /**
     * Replaces the boot splash artwork with the mod's own.
     *
     * Must be called from `ApplyInitPatches()`: the executable raises the
     * splash before it maps WHGame.dll, so the `MH_ALL_HOOKS` pass is too late.
     */
    void InstallSplashPatch();
} // namespace KCDC::Core::Boot::Splash
