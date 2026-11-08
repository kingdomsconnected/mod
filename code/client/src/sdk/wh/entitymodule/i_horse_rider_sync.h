#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;
    class C_Horse;

    /**
     * Abstract synchronization contract between a horse and its rider.
     *
     * RTTI identifies this namespace-qualified type as the primary base of
     * C_RiderSync. That implementation's table at 0x183EAACF0 contains 23
     * operations followed by the deleting-destructor override at slot 23,
     * with no concrete additions. C_RiderSync proves slots 15 and 16 by
     * returning its stored C_Actor rider and C_Horse pointers directly. Their
     * original source identifiers remain stripped. The next base begins at
     * +0x08.
     */
    class I_HorseRiderSync {
      public:
        virtual bool ForceDismount() = 0;              // slot 0, 0x1829F1C20
        virtual bool RequestRideState5() = 0;          // slot 1, 0x1829F2004
        virtual void RequestQuickStart() = 0;          // slot 2, 0x1829F1B30
        virtual void SetRiderSyncRequestFlag(bool) = 0; // slot 3, 0x1829F2D08
        virtual bool RequestRideState7To9(std::uint8_t state) = 0; // slot 4, 0x180CF8248
        virtual bool IsRidingOrSyncing() = 0;          // slot 5, 0x180CF8504; state 2 or 4-6
        virtual bool RequestRideState6() = 0;          // slot 6, 0x1829F0E64; the reins pulled hard
        virtual void ReportFootstepSound(std::uint32_t surfaceId) = 0; // slot 7, 0x180B703C0; the rider's AI perception footstep
        virtual void SubmitMountedMovementInput(const void *input, bool) = 0; // slot 8, 0x18059BC40
        virtual bool IsDismounting() = 0;              // slot 9, 0x1814D3AC4
        virtual bool IsRiderChargingShot() = 0;        // slot 10, 0x18151EFE0
        virtual bool IsRiderInControl() = 0;           // slot 11, 0x181302A0C; false mounting/dismounting or while chat-follow drives
        virtual bool IsRiding() = 0;                   // slot 12, 0x1829F0E80; state 2 or 10
        virtual bool IsRideState2AtGait4() = 0;        // slot 13, 0x1829F27E0
        virtual void SyncRiderAnimation() = 0;         // slot 14, 0x180AB1CE4
        /** The rider, stored directly. Slot 15. */
        virtual C_Actor *GetRiderRaw() = 0;
        /** The horse, stored directly. Slot 16. */
        virtual C_Horse *GetHorseRaw() = 0;
        virtual bool CanPerformMountedItemAction() = 0; // slot 17, 0x1810E30BC
        virtual void unk_fn0018() = 0;
        virtual void QueueEngineLockedUpdate() = 0;    // slot 19, 0x1814AE39C
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual ~I_HorseRiderSync() = default; // slot 23, +0x0B8
    };

    static_assert(sizeof(I_HorseRiderSync) == 0x8,
                  "I_HorseRiderSync size mismatch");
} // namespace SDK::wh::entitymodule
