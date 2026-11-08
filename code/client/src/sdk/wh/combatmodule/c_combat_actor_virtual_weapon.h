#pragma once

#include "../entitymodule/c_item_wrapper.h"
#include "c_combat_actor_object.h"

#include <cstddef>

namespace SDK::wh::combatmodule {
    /**
     * Combat-side representation of an actor's virtual weapon.
     *
     * RTTI proves direct C_CombatActorObject inheritance. Its table at
     * 0x183A5CE40 contains exactly the six inherited entries. Construction
     * allocates a C_ItemWrapper and stores it at +0x10; destruction releases
     * that exact object. Its sole allocation site proves the 0x20-byte layout.
     */
    class C_CombatActorVirtualWeapon : public C_CombatActorObject {
      public:
        ~C_CombatActorVirtualWeapon() override = default;

        entitymodule::C_ItemWrapper *GetItemWrapper() const {
            return m_pItemWrapper;
        }

        entitymodule::C_Item *GetItem() const {
            return m_pItemWrapper != nullptr ? m_pItemWrapper->GetItem()
                                             : nullptr;
        }

      private:
        entitymodule::C_ItemWrapper *m_pItemWrapper; // 0x10
        std::byte m_padding0018[0x8];                // 0x18
    };

    static_assert(sizeof(C_CombatActorVirtualWeapon) == 0x20,
                  "C_CombatActorVirtualWeapon size mismatch");
} // namespace SDK::wh::combatmodule
