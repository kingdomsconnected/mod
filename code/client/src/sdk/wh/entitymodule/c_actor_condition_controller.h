#pragma once

#include "../../types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;

    /**
     * An actor's condition controller, owned at `C_Actor+0x258`.
     *
     * The actor constructor builds it through the 0x80-byte factory at
     * 0x1808E0928, which installs this vtable.
     */
    class C_ActorConditionController {
      public:
        virtual ~C_ActorConditionController() = default;

        /**
         * Keeps the body's stance out of its condition. ApplyCondition
         * (0x1808ABC70) writes the condition's entry into layer 1 of the
         * actor's stance register (C_Actor+0x7A0, 0x1808ABD0C), where -1
         * overrides nothing. Init (0x180D5A0DC) gives every condition -1 but
         * injured, which gets stance 7: walk at most 0.8 m/s, run 2.3, no
         * sprint (soul_archetype_movement.xml).
         */
        void ClearConditionStances() {
            std::fill(m_stanceByCondition.first, m_stanceByCondition.last, -1);
        }

      private:
        // Factory 0x1808E0928 zeroes every member; destructor 0x1808E0808 frees the vectors.
        C_Actor *m_pActor;                       // 0x08, the actor the factory is built for
        std::uint8_t m_state0010;                // 0x10, factory zeroes
        std::byte m_padding0011[0x7];
        StdVector<std::byte> m_entries0018;      // 0x18, 0x40-byte elements destroyed by 0x180E531E0
        StdVector<std::byte> m_entries0030;      // 0x30, 0x40-byte elements destroyed by 0x180E531E0
        std::int32_t m_registration0048;         // 0x48, factory sets -1; when set, the dtor unregisters two callbacks from m_pActor
        std::byte m_padding004C[0x4];
        StdVector<std::uint32_t> m_values0050;   // 0x50, std::vector<4-byte> (dtor _Tidy)
        StdVector<std::int32_t> m_stanceByCondition; // 0x68, one stance per E_ActorCondition, read by ApplyCondition alone

        static void AssertLayout() {
            static_assert(offsetof(C_ActorConditionController, m_pActor) == 0x08, "C_ActorConditionController::m_pActor offset mismatch");
            static_assert(offsetof(C_ActorConditionController, m_entries0030) == 0x30, "C_ActorConditionController::m_entries0030 offset mismatch");
            static_assert(offsetof(C_ActorConditionController, m_registration0048) == 0x48, "C_ActorConditionController::m_registration0048 offset mismatch");
            static_assert(offsetof(C_ActorConditionController, m_stanceByCondition) == 0x68, "C_ActorConditionController::m_stanceByCondition offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorConditionController) == 0x80, "C_ActorConditionController size mismatch");
} // namespace SDK::wh::entitymodule
