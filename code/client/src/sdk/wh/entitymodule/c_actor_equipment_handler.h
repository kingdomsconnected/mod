#pragma once

#include "../../patterns.h"
#include "i_equipment_manager_listener.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;

    /**
     * Actor-side bridge for equipment state changes.
     *
     * RTTI and destruction prove I_EquipmentManagerListener as the direct
     * base. The table at 0x183A5D980 contains exactly the four inherited
     * entries and adds no operations. Construction stores the owning actor
     * at +0x10, while scalar deletion proves the exact 0x18-byte layout.
     */
    class C_ActorEquipmentHandler : public I_EquipmentManagerListener {
      public:
        ~C_ActorEquipmentHandler() override = default;

        /**
         * Puts what the equipment says the body wears back onto the body's
         * current character (0x1808D353C): stored weapons and sheaths into
         * the attachment manager's weapon slots, and the other attached
         * equipment. `C_Actor::SetPutAway`'s wake branch runs it right after
         * `RebuildClothing`, whenever the character was built anew. Leaves
         * what is held in the hands alone.
         */
        void ReattachToBody() { hook::this_call<void>(gPatterns.C_ActorEquipmentHandler__ReattachToBody, this); }

      private:
        std::uint32_t m_value0008; // 0x08, ctor 0x1808D2974 zeroes it
        std::byte m_padding000C[0x4];
        C_Actor *m_pActor; // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_ActorEquipmentHandler, m_value0008) == 0x08, "C_ActorEquipmentHandler::m_value0008 offset mismatch");
            static_assert(offsetof(C_ActorEquipmentHandler, m_pActor) == 0x10, "C_ActorEquipmentHandler::m_pActor offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorEquipmentHandler) == 0x18,
                  "C_ActorEquipmentHandler size mismatch");
} // namespace SDK::wh::entitymodule
