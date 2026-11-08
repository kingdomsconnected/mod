#pragma once

#include "../../types.h"
#include "../framework/c_action.h"
#include "i_actor_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_ActionActor;

    /**
     * What the ladder action is built from, at +0x50 (RTTI names it through
     * `C_ParamAction<S_LadderActionParams, ...>`; base ctor 0x180F4A03C
     * zeroes the entity id). Only the ladder's entity id is passed.
     */
    struct S_LadderActionParams {
        EntityId ladderEntityId; // 0x00
    };

    static_assert(sizeof(S_LadderActionParams) == 0x4, "S_LadderActionParams size mismatch");

    /**
     * The action that holds an actor on a ladder -- actor action type 0x22.
     *
     * The climb, get-on and get-off fragments are reachable only from inside
     * it; outside it a body walks up the rungs in a locomotion fragment. Its
     * factory allocates 0xF8 bytes and takes nothing but the ladder's entity
     * id, which is where the size and the offsets below come from.
     */
    // RTTI (base ctor 0x180F4A03C): C_ActorAction<I_ActorAction, C_ParamAction<S_LadderActionParams, C_Action<I_ActorAction>>>.
    class C_ActorActionLadder: public framework::C_ParamAction<S_LadderActionParams, framework::C_Action<I_ActorAction>> {
      public:
        /** The ladder this climb is on, as the factory stored it. */
        EntityId GetLadderEntityId() const {
            return GetParams().ladderEntityId;
        }

        /** The queue this was posted to, whose owner is therefore climbing. */
        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        /**
         * Positive climbs, negative descends, zero holds. The ladder update
         * hands it to the movement controller as the move request's local Y,
         * so it is a request rather than a measured speed --  and it is what
         * the climb fragment's blend reads.
         * `C_ActorStateExpansion::SetLadderClimbInput` is its only writer.
         */
        float GetClimbInput() const {
            return m_climbInput;
        }

        /**
         * How fast the body is climbing now, -1 (down) to 1 (up): the ladder
         * state's smoothed speed, which its climb step (0x1811D48B8) eases
         * towards the movement request's forward every frame. A player's
         * climb comes from `C_PlayerInput`'s request and never touches
         * `GetClimbInput`, so this is what says how a player is climbing.
         */
        float GetClimbVelocity() const {
            return m_climbVelocity;
        }

        /** Where on the ladder the body is: its rung and how far on towards the next. */
        float GetRungPosition() const {
            return static_cast<float>(m_rung) + m_ladder00A0;
        }

        /**
         * Stops the climb where the body is, as easing to no input would
         * over a few frames. The next step eases from here again.
         */
        void StopClimb() {
            m_climbVelocity = 0.0F;
        }

      private:
        std::byte m_actorObject0058[0x8];   // 0x58, C_ActorObject vtable (base ctor 0x180F4A03C)
        C_ActionActor *m_pActionActor;      // 0x60
        std::uint16_t m_state0068;          // 0x68, base ctor zeroes
        std::byte m_padding006A[0x2];       // 0x6A
        std::uint32_t m_state006C;          // 0x6C, base ctor zeroes
        void *m_value0070;                  // 0x70, ctor 0x180F49F4C zeroes
        std::uint16_t m_state0078;          // 0x78, ctor zeroes
        std::byte m_padding007A[0x6];       // 0x7A
        // The embedded ladder state, built by 0x180F49FA8 (which hashes "LadderFraction").
        void *m_ladder0080[2];              // 0x80, zeroed
        float m_climbVelocity;              // 0x90, the state's +0x10
        float m_ladder0094;                 // 0x94
        std::int32_t m_rung;                // 0x98, the rung the body is on
        std::int32_t m_rungCount;           // 0x9C
        float m_ladder00A0;                 // 0xA0, how far between this rung and the next
        float m_climbInput;                 // 0xA4
        void *m_ladder00A8;                 // 0xA8, zeroed
        std::uint8_t m_ladder00B0;          // 0xB0, zeroed
        std::byte m_padding00B1[0x7];       // 0xB1
        void *m_getOnTransition;            // 0xB8, zeroed
        void *m_getOffTransition;           // 0xC0, zeroed
        void *m_ladder00C8[2];              // 0xC8, zeroed
        std::byte m_opaque00D8[0x1C];       // 0xD8
        float m_ladder00F4;                 // 0xF4, set to -1.0

        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionLadder, m_actorObject0058) == 0x58, "C_ActorActionLadder::m_actorObject0058 offset mismatch");
            static_assert(offsetof(C_ActorActionLadder, m_pActionActor) == 0x60, "C_ActorActionLadder::m_pActionActor offset mismatch");
            static_assert(offsetof(C_ActorActionLadder, m_climbVelocity) == 0x90, "C_ActorActionLadder::m_climbVelocity offset mismatch");
            static_assert(offsetof(C_ActorActionLadder, m_rung) == 0x98, "C_ActorActionLadder::m_rung offset mismatch");
            static_assert(offsetof(C_ActorActionLadder, m_climbInput) == 0xA4, "C_ActorActionLadder::m_climbInput offset mismatch");
            static_assert(offsetof(C_ActorActionLadder, m_value0070) == 0x70, "C_ActorActionLadder::m_value0070 offset mismatch");
            static_assert(offsetof(C_ActorActionLadder, m_getOnTransition) == 0xB8, "C_ActorActionLadder::m_getOnTransition offset mismatch");
            static_assert(offsetof(C_ActorActionLadder, m_ladder00F4) == 0xF4, "C_ActorActionLadder::m_ladder00F4 offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorActionLadder) == 0xF8, "C_ActorActionLadder size mismatch");
} // namespace SDK::wh::entitymodule
