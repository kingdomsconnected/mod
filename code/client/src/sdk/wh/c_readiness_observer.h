#pragma once

#include <cstdint>

namespace SDK::wh {
    /**
     * A wait on the readiness service: `Start(mask, callback)` (0x180FC3EC0)
     * registers it with a one-frame delay and the service's timeout, and the
     * callback runs once every task in `mask` is ready or the timeout passes.
     * Start forwards to `StartEx(mask, timeout, delayFrames, callback)`
     * (0x180FC3F48), which stores the timeout as an absolute deadline at +0x20;
     * a negative timeout stores -100000, which the expiry test (0x1808BAE94)
     * reads as never.
     *
     * The game's load completions all wait on mask `kLoadMask`:
     * `C_SaveGameManager`'s "AfterGameLoad" observer (+0x198, started from
     * PostLoadGame and LoadSwitchSave), whose callback queues system event 21
     * and so takes the loading screen down; `C_NewGameHelper`'s
     * (0x180FC3688), whose callback marks the session started; and
     * `C_Game::OnLevelSwitchLoaded`.
     *
     * Only the entry is mapped; the layout is not.
     */
    class C_ReadinessObserver {
      public:
        /** The category mask every load completion waits on. */
        static constexpr std::uint32_t kLoadMask = 0x3F93;

        /**
         * What the skip-time screen waits on once the clock has stopped
         * (`C_SkipTime::OpenSkipTime` 0x180FC3618), its callback being
         * `CompletePostSkipTransition` (0x180F48C80), which gives the player
         * back. Only that site starts an observer with it.
         */
        static constexpr std::uint32_t kSkipTimeMask = 0x3F91;

        C_ReadinessObserver()                                       = delete;
        C_ReadinessObserver(const C_ReadinessObserver &)            = delete;
        C_ReadinessObserver &operator=(const C_ReadinessObserver &) = delete;
    };
} // namespace SDK::wh
