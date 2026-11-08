#pragma once

#include "../../types.h"
#include "../framework/c_action.h"
#include "c_action_actor.h"
#include "i_actor_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What the carry action is built from, copied whole into it at +0x50.
     *
     * `C_ActorCarryCorpse::RequestGrab` (0x18285E5B8) fills it: the body, mode
     * 0 for an ordinary grab, and the instant flag that skips the pick-up clip.
     */
    struct S_CarryCorpseActionParams {
        EntityId targetEntityId; // 0x00
        std::uint32_t mode;      // 0x04
        bool instant;            // 0x08
        std::byte m_pad0009[0x7];
    };

    static_assert(sizeof(S_CarryCorpseActionParams) == 0x10, "S_CarryCorpseActionParams size mismatch");

    /**
     * The action that holds a body on an actor's shoulder, from the grab to
     * the end of the put-down -- actor action category 9.
     *
     * `Enter` (0x182879F54) hands the parameters to `C_ActorCarryCorpse`,
     * which binds the body and queues the pick-up clip; `Exit` (0x18287A370)
     * ends the carry through `EndCarrying`, which is where the body is let
     * go. Every carry, however it was started, passes through both -- which
     * is what makes them the edges to watch.
     *
     * Its factory allocates 0x90 bytes (0x1828C27D0, constructor 0x182878520):
     * the framework action to +0x50, the parameters, then the `C_ActorObject`
     * base whose queue at +0x68 is the one `Enter` reads its carrier from.
     */
    // RTTI (base ctor 0x1828781C4): C_ActorAction<I_ActorAction, C_ParamAction<S_CarryCorpseActionParams, C_Action<I_ActorAction>>>.
    class C_ActorActionCarryCorpse: public framework::C_ParamAction<S_CarryCorpseActionParams, framework::C_Action<I_ActorAction>> {
      public:
        /** The body this carry is for, as the factory stored it. */
        EntityId GetTargetEntityId() const {
            return GetParams().targetEntityId;
        }

        /** The queue this was posted to, whose owner is therefore the carrier. */
        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

      private:
        std::byte m_actorObject0060[0x8];   // 0x60, C_ActorObject vtable
        C_ActionActor *m_pActionActor;      // 0x68
        std::uint16_t m_state0070;          // 0x70, base ctor 0x1828781C4 zeroes
        std::byte m_padding0072[0x2];       // 0x72
        std::uint32_t m_state0074;          // 0x74, base ctor zeroes
        void *m_value0078;                  // 0x78, ctor 0x182878520 zeroes
        std::uint16_t m_state0080;          // 0x80, ctor zeroes
        std::byte m_padding0082[0x6];       // 0x82
        std::byte m_interface0088[0x8];     // 0x88, a further C_ActorActionCarryCorpse vtable the ctor installs

        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionCarryCorpse, m_actorObject0060) == 0x60, "C_ActorActionCarryCorpse::m_actorObject0060 offset mismatch");
            static_assert(offsetof(C_ActorActionCarryCorpse, m_pActionActor) == 0x68, "C_ActorActionCarryCorpse::m_pActionActor offset mismatch");
            static_assert(offsetof(C_ActorActionCarryCorpse, m_value0078) == 0x78, "C_ActorActionCarryCorpse::m_value0078 offset mismatch");
            static_assert(offsetof(C_ActorActionCarryCorpse, m_interface0088) == 0x88, "C_ActorActionCarryCorpse::m_interface0088 offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorActionCarryCorpse) == 0x90, "C_ActorActionCarryCorpse size mismatch");
} // namespace SDK::wh::entitymodule
