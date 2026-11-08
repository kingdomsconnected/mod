#pragma once

#include "../framework/c_action.h"
#include "c_action_actor.h"
#include "i_actor_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The common base of the eight shooting sub-actions (aiming, charging,
     * instant charging, firing, reloading, uncharging, unloading,
     * transitioning to idle), constructor 0x182893C08.
     *
     * `UpdateShootingRestrictions1828A16BC` reads the body through the action
     * queue at +0x60 and keeps its restriction state in the flags at +0x90:
     * bit 0 the sub-action wants restrictions, bit 1 the weapon-dependent
     * one, bit 2 movement restriction scope 15 applied, bit 3 the
     * `no_horizontal_camera` filter applied, bit 4 the actor-model callback
     * registered.
     */
    class C_ActorActionShootingBase: public framework::C_Action<I_ActorAction> {
      public:
        /** The queue this sub-action runs on; its actor is the body. */
        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

      private:
        std::byte m_params0050[0x8];      // 0x50, wh::entitymodule::S_EmptyParams' vftable
        std::byte m_actorObject0058[0x8]; // 0x58, C_ActorObject
        C_ActionActor *m_pActionActor;    // 0x60
        std::byte m_opaque0068[0x18];     // 0x68
        void *m_pMainAction;              // 0x80, C_ActorActionShootingMain
        std::byte m_opaque0088[0x8];      // 0x88
        std::uint8_t m_restrictionFlags;  // 0x90
        std::byte m_padding0091[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionShootingBase, m_pActionActor) == 0x60, "C_ActorActionShootingBase::m_pActionActor offset mismatch");
            static_assert(offsetof(C_ActorActionShootingBase, m_pMainAction) == 0x80, "C_ActorActionShootingBase::m_pMainAction offset mismatch");
            static_assert(offsetof(C_ActorActionShootingBase, m_restrictionFlags) == 0x90, "C_ActorActionShootingBase::m_restrictionFlags offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorActionShootingBase) == 0x98, "C_ActorActionShootingBase size mismatch");
} // namespace SDK::wh::entitymodule
