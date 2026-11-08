#pragma once

#include "../framework/c_action.h"
#include "i_actor_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_ActionActor;
    class C_ActorActionShootingMain;

    /**
     * The shooting sub-action that draws a bow, from rest to full draw.
     *
     * Its factory (0x18288685C) allocates 0xC0 bytes. `Update` (0x1828A1044)
     * advances the draw by its rate each frame, publishes it to the animation
     * as a 0-1 parameter and to the expansion's charged tag, and at full draw
     * replaces itself with C_ActorActionShootingAiming -- the edge that raises
     * C_ActorActionShootingMain::IsAiming. A cancel request replaces it with
     * the uncharging sub-action instead.
     *
     * It is reached as C_ActorActionShootingMain's current sub-action, whose
     * concrete type is told by its vtable, not by that pointer's type.
     */
    // Base ctor 0x182893998 (RTTI): C_ActorAction<I_ActorAction, C_ParamAction<wh::entitymodule::S_EmptyParams, C_Action<I_ActorAction>>>.
    class C_ActorActionShootingCharging: public framework::C_Action<I_ActorAction> {
      public:
        /** The main action this draw runs inside (C_ActorActionShootingBase +0x80). */
        C_ActorActionShootingMain *GetMainAction() const {
            return m_pMainAction;
        }

        /** Set to abandon the draw; the next Update hands over to uncharging. */
        bool IsCancelRequested() const {
            return m_cancelRequested;
        }

        /** Set once the draw's animation is running and Update advances it. */
        bool IsDrawing() const {
            return m_drawing;
        }

        /** How far the bow is drawn, 0 to 1; at 1 the draw becomes aiming. */
        float GetCharge() const {
            return m_charge;
        }

        /**
         * The draw past which the shot may be released, read off the charge
         * fragment's animation event (CRC 0xB93C38DD) by
         * `AdjustChargeAnimationTiming` (0x18289D5D8).
         */
        float GetReleaseCharge() const {
            return m_releaseCharge;
        }

        /**
         * Whether the draw is past its release point: this sub-action's
         * `CanFire` (0x182895DC0, through 0x18289C734), and the value Update
         * writes into C_ActorShootingExpansion::IsCharged.
         */
        bool CanRelease() const {
            return m_charge > m_releaseCharge;
        }

      private:
        // C_ActorActionShootingBase ctor 0x182893C08 also sets C_Action+0x3C to 11.
        std::byte m_params0050[0x8];      // 0x50, wh::entitymodule::S_EmptyParams: only its vftable (0x183E784B0), installed by 0x182893998
        std::byte m_actorObject0058[0x8]; // 0x58, the C_ActorObject vtable slot the base ctor overwrites
        C_ActionActor *m_pActionActor;    // 0x60, C_ActorObject's queue; 0x182893998 registers this action with it
        std::uint16_t m_state0068;        // 0x68, 0x182893998 zeroes
        std::byte m_padding006A[0x2];
        std::uint32_t m_state006C;        // 0x6C, 0x182893998 zeroes
        void *m_value0070;              // 0x70, base ctor zeroes
        std::uint16_t m_state0078;      // 0x78, base ctor zeroes
        std::byte m_padding007A[0x6];
        C_ActorActionShootingMain *m_pMainAction; // 0x80
        std::uint32_t m_state0088;      // 0x88, base ctor zeroes
        std::byte m_padding008C[0x4];
        std::uint8_t m_flags0090;       // 0x90, base ctor clears the low five bits
        std::byte m_padding0091[0x7];
        bool m_cancelRequested; // 0x98
        bool m_drawing;         // 0x99
        std::byte m_padding009A[0x6];
        void *m_value00A0;      // 0xA0, CreateChargingAction 0x18288685C zeroes
        float m_charge;         // 0xA8, factory sets 0
        float m_drawRateBefore; // 0xAC, draw rate before the fragment's timing event; factory sets 1.0
        float m_drawRateAfter;  // 0xB0, draw rate after it; factory sets 1.0
        float m_drawDuration;   // 0xB4, filled by AdjustChargeAnimationTiming
        float m_eventFraction;  // 0xB8, the fraction the timing event sits at
        float m_releaseCharge; // 0xBC

        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionShootingCharging, m_pMainAction) == 0x80, "C_ActorActionShootingCharging::m_pMainAction offset mismatch");
            static_assert(offsetof(C_ActorActionShootingCharging, m_cancelRequested) == 0x98, "C_ActorActionShootingCharging::m_cancelRequested offset mismatch");
            static_assert(offsetof(C_ActorActionShootingCharging, m_charge) == 0xA8, "C_ActorActionShootingCharging::m_charge offset mismatch");
            static_assert(offsetof(C_ActorActionShootingCharging, m_pActionActor) == 0x60, "C_ActorActionShootingCharging::m_pActionActor offset mismatch");
            static_assert(offsetof(C_ActorActionShootingCharging, m_state006C) == 0x6C, "C_ActorActionShootingCharging::m_state006C offset mismatch");
            static_assert(offsetof(C_ActorActionShootingCharging, m_flags0090) == 0x90, "C_ActorActionShootingCharging::m_flags0090 offset mismatch");
            static_assert(offsetof(C_ActorActionShootingCharging, m_value00A0) == 0xA0, "C_ActorActionShootingCharging::m_value00A0 offset mismatch");
            static_assert(offsetof(C_ActorActionShootingCharging, m_eventFraction) == 0xB8, "C_ActorActionShootingCharging::m_eventFraction offset mismatch");
            static_assert(offsetof(C_ActorActionShootingCharging, m_releaseCharge) == 0xBC, "C_ActorActionShootingCharging::m_releaseCharge offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorActionShootingCharging) == 0xC0, "C_ActorActionShootingCharging size mismatch");
} // namespace SDK::wh::entitymodule
