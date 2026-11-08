#pragma once

#include "c_action_actor.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /** RTTI's C_ActorObject secondary base in actor actions: vptr and owner. */
    class C_ActorObject {
      public:
        virtual ~C_ActorObject() = default;
        C_ActionActor *GetActionActor() const {
            return m_actionActor;
        }

      private:
        C_ActionActor *m_actionActor;
        static void AssertLayout() {
            static_assert(offsetof(C_ActorObject, m_actionActor) == 0x08, "C_ActorObject action actor mismatch");
        }
    };
    static_assert(sizeof(C_ActorObject) == 0x10, "C_ActorObject size mismatch");
} // namespace SDK::wh::entitymodule
