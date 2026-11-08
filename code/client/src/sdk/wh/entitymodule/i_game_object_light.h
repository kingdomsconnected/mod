#pragma once

#include "../../cryengine/entitysystem/i_entity.h"
#include "../../cryengine/entitysystem/i_entity_proxy.h"

#include <cstddef>
#include <memory>

namespace SDK::wh::entitymodule {
    /**
     * Warhorse's lightweight game object: the entity proxy of tag points,
     * item slots, particle effects and shops, which have no `CGameObject`.
     *
     * RTTI proves IEntityProxy at +0x00 and
     * std::enable_shared_from_this<IComponent> at +0x08. The complete table
     * at 0x183A4A3D8 contains exactly the 22 inherited IEntityProxy entries
     * and adds no virtual contracts. Its `GetType` (0x181A83AC0) answers
     * `EEntityProxy::GameObjectLight`; its `Init` (0x1804579E0) stores the
     * entity at +0x28 and runs slot 17, and its `Done` (0x1808E3728) runs slot
     * 18 and clears the entity again. Exact extent 0x30.
     */
    class I_GameObjectLight
        : public SDK::IEntityProxy
        , public std::enable_shared_from_this<SDK::IComponent> {
      public:
        ~I_GameObjectLight() override = default;

        /** The entity this proxy belongs to, from `Init` until `Done`. */
        IEntity *GetEntity() const {
            return m_pEntity;
        }

      private:
        // C_ItemSlot's ctor (0x1803A6744) zeroes both before the entity slot.
        void *m_value0018; // 0x18
        void *m_value0020; // 0x20
        IEntity *m_pEntity; // 0x28

        static void AssertLayout() {
            static_assert(offsetof(I_GameObjectLight, m_pEntity) == 0x28, "I_GameObjectLight::m_pEntity offset mismatch");
            static_assert(offsetof(I_GameObjectLight, m_value0018) == 0x18, "I_GameObjectLight::m_value0018 offset mismatch");
        }
    };

    static_assert(sizeof(I_GameObjectLight) == 0x30, "I_GameObjectLight size mismatch");
} // namespace SDK::wh::entitymodule
