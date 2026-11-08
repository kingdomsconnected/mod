#pragma once

namespace KCDC::Core {
    /**
     * Patches the executable, before anything of the game exists.
     *
     * Call from InitClient: the client DLL comes up during the executable's
     * CRT startup, which is the last point ahead of everything patched here.
     */
    void ApplyInitPatches();

    /**
     * Loads WHGame.dll on the calling thread.
     *
     * Call from InitClient, which runs during the executable's CRT startup:
     * the game's main() has not run, so the pattern sweep and every hook that
     * follows land before the engine exists and nothing races anything.
     * Waiting for the game to load the module instead puts that work on a
     * thread of its own, and the engine reaches its intro-movie gate and its
     * front end well before it finishes.
     *
     * Returns the module, or null if it could not be loaded, leaving the
     * caller to fall back on waiting for the game to load it.
     */
    void *LoadGameModule();
} // namespace KCDC::Core

namespace KCDC::Core::Boot {
    /**
     * Ends the boot logo reel, if one is playing.
     *
     * Idempotent: the engine guards the teardown on its own
     * m_pIntroMovieRenderer, so this is a no-op before the reel exists and
     * after it is gone. Only wanted when the suppression hook missed its
     * window, which is why the loading pump and CompleteInit both call it.
     */
    void KillIntroMovie();
} // namespace KCDC::Core::Boot
