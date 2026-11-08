#pragma once

#include "../framework/c_action.h"
#include "c_action_actor.h"
#include "i_actor_action.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * The part `C_ActorActionDead`, `C_ActorActionUnconscious` and
     * `C_ActorActiontRagdoll` share: the queue they were posted to, and
     * therefore the body they are about.
     *
     * A pointer target rather than a layout -- only this one field is mapped
     * and none of the three sizes is established -- so it is never
     * constructed, copied or arrayed, only reached through.
     *
     * The offset is proven three times over, in the `Enter` of each of the
     * three actions (0x180E78768, 0x182898668, 0x180E79C68): each reads its
     * actor as `actionActor->GetActor()` off `self+0x58`. Note that
     * `C_ActorActionLadder` is *not* one of these -- it keeps its own queue
     * pointer at +0x60 -- so this must not be widened to cover it.
     */
    class C_ActorBodyAction: public framework::C_Action<I_ActorAction> {
      public:
        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        /** The body this action is about, or null before the queue is wired. */
        C_Actor *GetActor() const {
            return m_pActionActor != nullptr ? m_pActionActor->GetActor() : nullptr;
        }

      private:
        std::byte m_actorObject0050[0x8]; // 0x50, C_ActorObject vtable (actor-action base ctor 0x1809CDA38)
        C_ActionActor *m_pActionActor; // 0x58

        static void AssertLayout() {
            static_assert(offsetof(C_ActorBodyAction, m_pActionActor) == 0x58, "C_ActorBodyAction::m_pActionActor offset mismatch");
        }
    };
} // namespace SDK::wh::entitymodule
