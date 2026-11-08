#pragma once

#include "../action/i_game_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CAIProxy, vtable 0x183A77538. `Update`, slot 1 at 0x1808372A8,
     * sends the AI's movement request every frame without
     * CheckUpdateStatus's early return.
     */
    class CAIProxy {
      public:
        /** The game object of the actor this proxy drives (CAIProxy::m_pGameObject). */
        IGameObject *GetGameObject() const {
            return m_pGameObject;
        }

      private:
        void *m_pVTable;                    // 0x00, CAIProxy::vftable over _reference_target<int>
        std::int32_t m_nRefCounter;         // 0x08, _reference_target<int>
        std::byte m_opaque000C[0x4];        // 0x0C
        void *m_pWeaponEventListenerVTable; // 0x10, second base IWeaponEventListener (destructor 0x180AE6CAC)
        IGameObject *m_pGameObject; // 0x18, read by CAIProxy::GetActor 0x1806444A8

        static void AssertLayout() {
            static_assert(offsetof(CAIProxy, m_nRefCounter) == 0x08, "CAIProxy::m_nRefCounter offset mismatch");
            static_assert(offsetof(CAIProxy, m_pWeaponEventListenerVTable) == 0x10, "CAIProxy::m_pWeaponEventListenerVTable offset mismatch");
            static_assert(offsetof(CAIProxy, m_pGameObject) == 0x18, "CAIProxy::m_pGameObject offset mismatch");
        }
    };
} // namespace SDK
