#pragma once

#include "../../patterns.h"
#include "../entitymodule/i_movement_input_listener.h"
#include "c_combat_actor_object.h"
#include "i_combat_player_controller.h"

#include <cstddef>
#include <cstdint>

#include <utils/hooking/hooking.h>

#include <utils/hooking/hooking.h>

namespace SDK::wh::combatmodule {
    /**
     * Concrete local-player combat and movement-input controller.
     *
     * RTTI proves I_CombatPlayerController at +0x00,
     * I_MovementInputListener at +0x08, and C_CombatActorObject at +0x10.
     * Its three tables implement exactly their respective base contracts and
     * add no operations. Default construction and sized deletion prove the
     * complete 0x100-byte layout.
     */
    class C_CombatPlayerController: public I_CombatPlayerController, public wh::entitymodule::I_MovementInputListener, public C_CombatActorObject {
      public:
        ~C_CombatPlayerController() override = default;

        /**
         * The attack key's press, `BeginAttack(inputClassId)` (0x180D4AAC4):
         * raises the fighting stance, sets the attack request bit and the
         * attack input class, and moves the fighter into reading-to-strike.
         */
        void BeginAttack(std::uint32_t inputClassId) {
            hook::this_call<void>(gPatterns.C_CombatPlayerController__BeginAttack, this, inputClassId);
        }

        /**
         * The attack key's release, `ReleaseAttack(inputClassId)`
         * (0x180D4A99C): builds and queues the strike through `PerformAttack`,
         * then ends the press, or aborts when the fighter left combat mode.
         */
        void ReleaseAttack(std::uint32_t inputClassId) {
            hook::this_call<void>(gPatterns.C_CombatPlayerController__ReleaseAttack, this, inputClassId);
        }

        /**
         * The block key's press. Gated by `CanBeginGuard`, raises the fighting
         * stance through `EnsureFightingStance`, sets the model's block request
         * bit and asks `RequestGuard(GUARD_REQUEST_GUARD, GUARD_SOURCE_PLAYER)`.
         * A fighter with an opponent skips the stance and the bit and only
         * asks the guard. Refused presses change nothing.
         */
        void BeginGuard() {
            hook::this_call<void>(gPatterns.C_CombatPlayerController__BeginGuard, this);
        }

        /**
         * The block key's release. Clears the block request bit and arms the
         * release timer; `Update` (0x1809D4244) drops the guard once it runs
         * out, so the arms come down on the engine's own delay.
         */
        void EndGuard() {
            hook::this_call<void>(gPatterns.C_CombatPlayerController__EndGuard, this);
        }

        /**
         * Whether a release is counting down: `EndGuard` sets this and arms
         * the timer at +0x80 -- again on every call -- and `Update` clears it
         * as the timer runs out and drops the guard (`RequestGuard(0, 0)`);
         * `BeginGuard` clears it too.
         */
        bool IsGuardReleasePending() const {
            return m_guardReleasePending != 0;
        }

        /**
         * Ends a pressed attack that produced no action: clears the attack
         * input class and request bit, and the controller's own press state
         * at +0x99, +0xF0 and +0xF8.
         */
        void AbortAttack() {
            hook::this_call<void>(gPatterns.C_CombatPlayerController__AbortAttack, this);
        }

      private:
        // Constructor 0x180B3EB40.
        void *m_ptrs0020[4];                // 0x20; ctor 0
        std::uint8_t m_flag0040;            // 0x40; ctor 0
        std::byte m_padding0041[0x7];
        std::byte m_disableLockContext[0x20]; // 0x48; wh::shared::C_ModelContextProperty<E_CombatDisableLockContext,bool,0>: vftable, C_Signal (unk_185665AC0) at +0x08, then state sub_180B3EDD8 initialises
        std::uint32_t m_values0068[2];      // 0x68; ctor 0
        std::uint32_t m_values0070[2];      // 0x70; ctor 0
        std::int32_t m_value0078;           // 0x78; ctor -1
        std::byte m_padding007C[0x4];
        std::int64_t m_time0080;            // 0x80; ctor -100000
        std::uint8_t m_guardReleasePending; // 0x88; ctor 0, see IsGuardReleasePending
        std::byte m_padding0089[0x7];
        std::int64_t m_time0090;            // 0x90; ctor -100000
        std::uint8_t m_state0098;           // 0x98; ctor 0
        std::uint8_t m_pressState0099;      // 0x99; cleared by AbortAttack
        std::byte m_padding009A[0x6];
        std::int64_t m_times00A0[4];        // 0xA0; ctor -100000
        std::uint8_t m_flag00C0;            // 0xC0; ctor 0
        std::byte m_padding00C1[0x7];
        std::int64_t m_time00C8;            // 0xC8; ctor -100000
        void *m_ptrs00D0[3];                // 0xD0; ctor 0
        void *m_pStorage00E8;               // 0xE8; 1-byte pool allocation
        std::int64_t m_pressTime00F0;       // 0xF0; ctor -100000, cleared by AbortAttack
        std::uint16_t m_pressState00F8;     // 0xF8; ctor 0, cleared by AbortAttack
        std::byte m_padding00FA[0x6];

        static void AssertLayout() {
            static_assert(offsetof(C_CombatPlayerController, m_disableLockContext) == 0x48, "C_CombatPlayerController::m_disableLockContext offset mismatch");
            static_assert(offsetof(C_CombatPlayerController, m_value0078) == 0x78, "C_CombatPlayerController::m_value0078 offset mismatch");
            static_assert(offsetof(C_CombatPlayerController, m_pressState0099) == 0x99, "C_CombatPlayerController::m_pressState0099 offset mismatch");
            static_assert(offsetof(C_CombatPlayerController, m_times00A0) == 0xA0, "C_CombatPlayerController::m_times00A0 offset mismatch");
            static_assert(offsetof(C_CombatPlayerController, m_pStorage00E8) == 0xE8, "C_CombatPlayerController::m_pStorage00E8 offset mismatch");
            static_assert(offsetof(C_CombatPlayerController, m_pressTime00F0) == 0xF0, "C_CombatPlayerController::m_pressTime00F0 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatPlayerController) == 0x100, "C_CombatPlayerController size mismatch");
} // namespace SDK::wh::combatmodule
