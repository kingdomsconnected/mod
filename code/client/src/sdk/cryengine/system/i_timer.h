#pragma once

namespace SDK {
    /** Which clock `ITimer::GetCurrTime` reads. */
    enum class ETimer : int {
        Game = 0,
        UI   = 1,
    };

    /**
     * CryEngine's frame timer, `gEnv->pTimer` (`CTimer`, vtable 0x183A6CCD0).
     * The MSVC scalar deleting destructor comes first; slot 3 is proven by
     * `C_Dice::InitializeDiceSession` (0x182E93D30), which calls +0x18 with
     * `ETIMER_GAME` in EDX and takes the float back in XMM0, and slot 9 by
     * reading `CTimer`'s own body.
     */
    class ITimer {
      public:
        virtual ~ITimer() = default;

        virtual void ResetTimer()         = 0; // +0x08
        virtual void UpdateOnFrameStart() = 0; // +0x10

        /** +0x18. Seconds on `which` clock at the start of this frame. */
        virtual float GetCurrTime(ETimer which) const = 0;

        // KCD2's table departs from the published order past here; these are
        // kept by position only.
        virtual void Opaque04() = 0; // +0x20
        virtual void Opaque05() = 0; // +0x28
        virtual void Opaque06() = 0; // +0x30
        virtual void Opaque07() = 0; // +0x38
        virtual void Opaque08() = 0; // +0x40

        /**
         * +0x48, `CTimer::GetFrameTime` (0x1805D5F7C). The last frame's length
         * in seconds on `which` clock: zero while the timer is disabled, and
         * on the game clock zero while the game is paused too.
         */
        virtual float GetFrameTime(ETimer which) const = 0;
    };
} // namespace SDK
