#pragma once

#include "c_actor.h"
#include "c_base_action.h"

#include <cstddef>
#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    /**
     * Native player locomotion; allocated as 0xF8 at 0x18113A11C. Only
     * C_Player's setup creates one (held at C_Player+0xB90); nothing in it is
     * particular to C_Player, so any actor can run it.
     */
    class C_PlayerMovementAction: public C_BaseAction {
      public:
        /** A new action at refcount zero; the caller AddRefs and queues it. */
        static C_PlayerMovementAction *Create(EntityId entityId) {
            auto *storage = hook::call<void *>(gPatterns.CryActionAllocate, sizeof(C_PlayerMovementAction));
            if (storage == nullptr) {
                return nullptr;
            }
            return hook::this_call<C_PlayerMovementAction *>(gPatterns.C_PlayerMovementAction__Construct, storage, entityId);
        }

        C_Actor *ResolveActor() {
            return hook::this_call<C_Actor *>(gPatterns.C_PlayerMovementAction__ResolveActor, this);
        }

      private:
        std::byte m_opaque0098[0x8];
        C_Actor *m_pOwnerActor; // 0xA0, resolved and cached by 0x18094A190
        std::byte m_opaque00A8[0x18];
        // Initialized by the constructor 0x18113A22C.
        SDK::Vec3 m_vector00C0;      // 0xC0
        std::uint32_t m_state00CC;   // 0xCC
        std::uint64_t m_state00D0;   // 0xD0, 6 by default
        std::uint64_t m_state00D8[3]; // 0xD8
        std::uint16_t m_state00F0;   // 0xF0
        bool m_state00F2;            // 0xF2
        std::byte m_padding00F3[0x5];

        static void AssertLayout() {
            static_assert(offsetof(C_PlayerMovementAction, m_pOwnerActor) == 0xA0, "C_PlayerMovementAction owner offset mismatch");
        }
    };

    static_assert(sizeof(C_PlayerMovementAction) == 0xF8, "C_PlayerMovementAction size mismatch");
} // namespace SDK::wh::entitymodule
