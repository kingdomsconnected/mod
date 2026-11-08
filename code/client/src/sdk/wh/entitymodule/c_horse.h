#pragma once

#include "../shared/c_model_property.h"
#include "../shared/s_model_value.h"
#include "c_animal.h"
#include "i_horse_move_adapter.h"
#include "i_horse_rider_sync.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Horse;
    struct HorseRuntimeDataLayout;

    /** ABI overlay for C_Horse's constructor-initialized inline state. */
    struct HorseInlineStateLayout {
        Vec3 m_vectors[5]; // 0x00, five independently initialized Vec3 values
        std::byte m_opaque003C[0x34];
    };

    static_assert(sizeof(HorseInlineStateLayout) == 0x70,
                  "HorseInlineStateLayout size mismatch");

    /** Constructor-proven inline horse-stamina integration state. */
    struct HorseStaminaStateLayout {
        HorseRuntimeDataLayout *m_pRuntimeData; // 0x00, owning runtime data
        std::uint32_t m_parameterId;            // 0x08, horse_stamina
        std::uint32_t m_padding000C;
        std::uint32_t m_state0010;
        float m_cachedStaminaBand; // 0x14, floor((1.0f - stamina) * 4.0f)
        float m_state0018;         // 0x18, initialized to -1.0f
        std::uint8_t m_flags001C;
        std::byte m_padding001D[0x3];
        std::byte m_opaque0020[0x28];
        std::int32_t m_state0048; // initialized to -1
        std::int32_t m_state004C; // initialized to -1

        float GetCachedStaminaBand() const { return m_cachedStaminaBand; }
    };

    static_assert(sizeof(HorseStaminaStateLayout) == 0x50,
                  "HorseStaminaStateLayout size mismatch");

    /**
     * The per-frame upkeep block embedded at runtime +0x420, which
     * `HorseRuntimeData::Update` ticks once a frame after it has submitted the
     * movement request. Only its back pointer and the ridden-idle timer are
     * understood; the rest belongs to the five other passes of that tick.
     */
    struct HorseRiddenUpkeepLayout {
        HorseRuntimeDataLayout *m_pRuntimeData; // 0x00, owning runtime data
        std::byte m_opaque0008[0x14];
        // Seconds the horse has stood under a player rider. Past the
        // `HorseCVars + 0xE4` limit the horse is snapped onto its navmesh.
        float m_riddenIdleSeconds; // 0x1C
        std::byte m_opaque0020[0x8];

        HorseRuntimeDataLayout *GetRuntimeData() const { return m_pRuntimeData; }
    };

    static_assert(sizeof(HorseRiddenUpkeepLayout) == 0x28,
                  "HorseRiddenUpkeepLayout size mismatch");

    /**
     * ABI overlay for C_Horse's stripped, non-polymorphic runtime allocation.
     * This is a descriptive SDK layout name, not a recovered source type.
     */
    struct HorseRuntimeDataLayout {
        // Laid out by HorseRuntimeData::CtorProperties (0x180BE0F00): the
        // rider-sync property, then five flag and three word values, each
        // with its own signal.
        wh::shared::C_ModelProperty<I_HorseRiderSync *> m_riderSync; // 0x000
        wh::shared::S_ModelValue<bool> m_flagValues[5];          // 0x030
        wh::shared::S_ModelValue<std::uint32_t> m_wordValues[3]; // 0x0A8
        HorseRuntimeDataLayout *m_pSelf; // 0x0F0
        C_Horse *m_pHorse;               // 0x0F8
        // The rider's input driving this animal, set by
        // `C_Horse::SetMoveAdapter` (0x1810EA99C); `Update` calls its slot 5.
        I_HorseMoveAdapter *m_pMoveAdapter; // 0x100
        // Four requests the horse's own reactions consume, each a byte set
        // to 1. The throw-off update (0x180CF82C4) raises 0x108 once the
        // horse has been scared for 6 s and rears for it or for 0x10A,
        // throws its rider for 0x109 and drops 0x109 every frame; the fast
        // stop (0x1806CF030) halts for 0x10B and drops it. The rider's input
        // raises 0x109 and 0x10B (`ApplyHorseMovementCommands`, 0x180A50016
        // and 0x180A4FFFC), and the throw-off raises 0x10B in place of a rear
        // its actor model refuses (0x180CF8422).
        std::uint8_t m_scaredTooLong;     // 0x108
        std::uint8_t m_throwOffRequested; // 0x109
        // Set to ask the animal to rear and throw whoever is on it off. The
        // game's own `RearAndThrowDown` script binding writes exactly this and
        // nothing else.
        std::uint8_t m_throwDownRequested; // 0x10A
        std::uint8_t m_fastStopRequested;  // 0x10B
        // Raised by `C_RiderPlayerInput::ApplyHorseMovementCommands` for the
        // rider's jump, and cleared by `ApplyPendingJumpRequest` (0x1806CFABC)
        // as it puts the jump into the movement request.
        std::uint8_t m_jumpRequested; // 0x10C
        std::byte m_padding010D[0x3];
        // The animal's own speed, which its tag pass reads to choose a MoveDir
        // tag and clears the group when it is zero.
        float m_speed; // 0x110
        float m_previousSpeed; // 0x114, last frame's m_speed
        // The movement command, as the rider's input states it every frame
        // through `SubmitHorseMoveCommand` (0x180A5019C): a desired speed
        // (-1 for the gait's own), the gait's move style (-1 for none, which
        // brings the horse to a stop), the input flags (0x04 forward held,
        // 0x08 back held), and whether to back up. `UpdateHorseMovementSpeed`
        // (0x1806CEC98) turns it into m_speed, and copies the backing request
        // into m_movingBackward only while the horse stands.
        float m_desiredSpeedOverride;     // 0x118
        std::int32_t m_gaitStyleId;       // 0x11C
        std::uint32_t m_commandFlags;     // 0x120
        std::uint8_t m_movingBackward;    // 0x124
        std::uint8_t m_backwardRequested; // 0x125
        std::byte m_padding0126[0x2];
        std::uint64_t m_state0128; // 0x128
        std::uint64_t m_state0130; // 0x130
        bool m_state0138;          // 0x138
        std::byte m_padding0139[0x3];
        Vec3 m_vector013C;         // 0x13C
        HorseRuntimeDataLayout *m_pSelf0148; // 0x148, the HorseRuntimeData constructor's back-pointer
        bool m_state0150;          // 0x150
        std::byte m_padding0151[0x7];
        std::byte m_opaque0158[0x80];  // 0x158, built by 0x180BE0C8C, which connects to the model's signals
        HorseRuntimeDataLayout *m_pSelf01D8; // 0x1D8
        HorseRuntimeDataLayout *m_pSelf01E0; // 0x1E0
        bool m_state01E8;          // 0x1E8
        std::byte m_padding01E9[0x3];
        std::uint32_t m_state01EC; // 0x1EC
        std::byte m_opaque01F0[0x230]; // 0x1F0, built by 0x180BE0D80
        HorseRiddenUpkeepLayout m_riddenUpkeep; // 0x420
        HorseStaminaStateLayout m_staminaState; // 0x448
        // The rider-sync listener the constructor connects to m_riderSync's
        // signal with callback 0x180A1C728.
        HorseRuntimeDataLayout *m_pRiderSyncListenerOwner; // 0x498
        std::uint32_t m_riderSyncConnection; // 0x4A0
        std::byte m_padding04A4[0x4];
        HorseRuntimeDataLayout *m_pSelf04A8; // 0x4A8
        std::uint64_t m_state04B0; // 0x4B0
        std::uint64_t m_state04B8; // 0x4B8
        float m_state04C0;         // 0x4C0, 0.1 by default
        float m_state04C4;         // 0x4C4, FLT_MAX by default

        I_HorseRiderSync *GetRiderSync() const { return m_riderSync.GetValue(); }
        C_Horse *GetHorse() const { return m_pHorse; }
        float GetSpeed() const { return m_speed; }

        /**
         * Seeds the animal's own speed. `UpdateHorseMovementSpeed`
         * (0x1806CEC98) accelerates it toward the rider command's each frame,
         * so a value written here is where that frame's acceleration starts
         * from, and `ApplyRuntimePaceAndDelta` (0x1806CF3D4) states the gait
         * and delta move from it.
         */
        void SetSpeed(float speed) { m_speed = speed; }

        /**
         * Starts one of the horse's own reaction actions -- the clip and the
         * locomotion it carries -- the way its reaction updates do before
         * they ask the rider sync for the ride state: 1 idle2move (ride
         * state 4), 2 move2idle (5), 5 the fast stop (6), 3 the rear (7), 4
         * the forced throw (8). False when the action controller refused it.
         */
        bool StartReactionAction(std::uint8_t reaction) {
            return hook::call<bool>(gPatterns.TryStartHorseReactionAction, this, reaction);
        }

        /** Drops the rear and throw requests the throw-off update would have consumed this frame (0x108-0x10A). */
        void ClearThrowOffRequests() {
            m_scaredTooLong      = 0;
            m_throwOffRequested  = 0;
            m_throwDownRequested = 0;
        }

        /** Drops the request the fast stop would have consumed this frame (0x10B). */
        void ClearFastStopRequest() {
            m_fastStopRequested = 0;
        }
        bool IsJumpRequested() const {
            return m_jumpRequested != 0;
        }

        /**
         * Asks for a jump the way the rider's command does: the flag
         * `ApplyPendingJumpRequest` (0x1806CFABC) consumes on the horse's
         * next update. A frame sooner than the rider input's own latch,
         * which has to go through the rider's PreUpdate first.
         */
        void RequestJump() { m_jumpRequested = 1; }
        void ClearJumpRequest() { m_jumpRequested = 0; }
        I_HorseMoveAdapter *GetMoveAdapter() const {
            return m_pMoveAdapter;
        }
        bool IsMovingBackward() const { return m_movingBackward != 0; }

        /** The gait the rider's command last asked for: a move style id, or -1 to stand. */
        std::int32_t GetGaitStyleId() const { return m_gaitStyleId; }

        /**
         * States the command a rider's input states each frame, the way
         * `SubmitHorseMoveCommand` (0x180A5019C) writes it: walk or back up
         * at `gaitStyleId`'s pace, or stand with -1.
         */
        void SetMoveCommand(std::int32_t gaitStyleId, bool backward) {
            m_gaitStyleId          = gaitStyleId;
            m_commandFlags         = gaitStyleId == -1 ? 0U : (backward ? 0x08U : 0x04U);
            m_backwardRequested    = backward ? 1 : 0;
            m_desiredSpeedOverride = -1.0F;
        }

        /** Asks the animal to rear and throw its rider off. */
        void RequestThrowDown() { m_throwDownRequested = 1; }
        HorseStaminaStateLayout *GetStaminaState() { return &m_staminaState; }
        const HorseStaminaStateLayout *GetStaminaState() const {
            return &m_staminaState;
        }
    };

    static_assert(sizeof(HorseRuntimeDataLayout) == 0x4C8,
                  "HorseRuntimeDataLayout size mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_scaredTooLong) == 0x108, "HorseRuntimeDataLayout::m_scaredTooLong offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_throwOffRequested) == 0x109, "HorseRuntimeDataLayout::m_throwOffRequested offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_throwDownRequested) == 0x10A,
                  "HorseRuntimeDataLayout::m_throwDownRequested offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_fastStopRequested) == 0x10B, "HorseRuntimeDataLayout::m_fastStopRequested offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_pMoveAdapter) == 0x100, "HorseRuntimeDataLayout::m_pMoveAdapter offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_jumpRequested) == 0x10C, "HorseRuntimeDataLayout::m_jumpRequested offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_speed) == 0x110,
                  "HorseRuntimeDataLayout::m_speed offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_movingBackward) == 0x124,
                  "HorseRuntimeDataLayout::m_movingBackward offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_desiredSpeedOverride) == 0x118,
                  "HorseRuntimeDataLayout::m_desiredSpeedOverride offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_gaitStyleId) == 0x11C,
                  "HorseRuntimeDataLayout::m_gaitStyleId offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_commandFlags) == 0x120,
                  "HorseRuntimeDataLayout::m_commandFlags offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_backwardRequested) == 0x125,
                  "HorseRuntimeDataLayout::m_backwardRequested offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_riddenUpkeep) == 0x420,
                  "HorseRuntimeDataLayout::m_riddenUpkeep offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_staminaState) == 0x448,
                  "HorseRuntimeDataLayout::m_staminaState offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_flagValues) == 0x30, "HorseRuntimeDataLayout::m_flagValues offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_wordValues) == 0xA8, "HorseRuntimeDataLayout::m_wordValues offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_pSelf) == 0xF0, "HorseRuntimeDataLayout::m_pSelf offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_pSelf0148) == 0x148, "HorseRuntimeDataLayout::m_pSelf0148 offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_pSelf01D8) == 0x1D8, "HorseRuntimeDataLayout::m_pSelf01D8 offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_pRiderSyncListenerOwner) == 0x498, "HorseRuntimeDataLayout::m_pRiderSyncListenerOwner offset mismatch");
    static_assert(offsetof(HorseRuntimeDataLayout, m_state04C4) == 0x4C4, "HorseRuntimeDataLayout::m_state04C4 offset mismatch");

    /**
     * Concrete horse actor.
     *
     * C_Horse's primary table at 0x183A6E508 contains exactly the 280 slots
     * inherited from C_Animal; it adds no primary virtual entries. The
     * complete object is 0xA60 bytes, with IGameObjectView and
     * IGameObjectProfileManager secondary vptrs at +0x40 and +0x48. Its
     * derived tail starts with an owned 0x4C8-byte horse-runtime allocation at
     * +0x9E8, followed by a 0x70-byte inline state block at +0x9F0. The runtime
     * allocation begins with a C_ModelProperty<I_HorseRiderSync*> whose current
     * value is at runtime +0x08, stores the owning C_Horse at runtime +0xF8,
     * and embeds an exact 0x50-byte horse_stamina integration state at +0x448;
     * its source class name remains opaque because it has no RTTI.
     */
    class C_Horse : public C_Animal {
      public:
        static constexpr const char *ActorClassName = "C_Horse";
        static constexpr ActorClass ActorClassId = ActorClass_Horse;

        /**
         * `SetBaseRotationWithFlags` flag: write the base rotation without
         * turning the rider's angles with it (0x181ECC550).
         */
        static constexpr std::uint32_t kBaseRotationWithoutRider = 0x200000;

        ~C_Horse() override = default;

        HorseRuntimeDataLayout *GetHorseRuntimeData() const {
            return m_pHorseRuntimeData;
        }

        I_HorseRiderSync *GetRiderSync() const {
            return m_pHorseRuntimeData != nullptr
                       ? m_pHorseRuntimeData->GetRiderSync()
                       : nullptr;
        }

        C_Actor *GetRider() const {
            auto *riderSync = GetRiderSync();
            return riderSync != nullptr ? riderSync->GetRiderRaw() : nullptr;
        }

        /** Whether anybody is in the saddle. The game's own `HasRider`. */
        bool HasRider() const {
            return GetRiderSync() != nullptr;
        }

        /**
         * Whether the animal can be got onto at all, which is what the game's
         * own `IsMountable` answers: on its feet and alive. Occupancy is a
         * separate question the engine asks elsewhere.
         */
        bool IsMountable() {
            return !IsFallen() && GetHealth() > 0.0F;
        }

        /**
         * Rears and throws whoever is riding off, the same way the game's own
         * `RearAndThrowDown` binding does -- by asking, not by posing: the
         * animal plays it and the rider comes off with it.
         */
        void RearAndThrowDown() {
            if (auto *runtime = GetHorseRuntimeData()) {
                runtime->RequestThrowDown();
            }
        }

        HorseInlineStateLayout *GetInlineState() { return &m_inlineState; }

        const HorseInlineStateLayout *GetInlineState() const {
            return &m_inlineState;
        }

      private:
        HorseRuntimeDataLayout *m_pHorseRuntimeData; // 0x9E8, owned
        HorseInlineStateLayout m_inlineState; // 0x9F0
    };

    static_assert(sizeof(C_Horse) == 0xA60, "C_Horse size mismatch");
} // namespace SDK::wh::entitymodule
