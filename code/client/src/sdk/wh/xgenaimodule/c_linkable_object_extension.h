#pragma once

#include "../../cryengine/action/i_actor.h"
#include "c_linkable_object_holder.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::xgenaimodule {
    /**
     * Shared game-object-extension base for linkable XGenAI world objects.
     *
     * RTTI preserves the hierarchy through
     * CGameObjectExtensionHelper<C_LinkableObjectExtension,
     * IGameObjectExtension, 64>. Its table at 0x183A651E8 contains exactly the
     * 29 IGameObjectExtension entries. Factory allocation and sized deletion
     * prove the complete 0x58-byte layout, including the holder at +0x40.
     */
    class C_LinkableObjectExtension: public SDK::IGameObjectExtension {
      public:
        ~C_LinkableObjectExtension() override = default;

        C_LinkableObjectHolder *GetLinkHolder() {
            return &m_linkHolder;
        }
        const C_LinkableObjectHolder *GetLinkHolder() const {
            return &m_linkHolder;
        }

      private:
        C_LinkableObjectHolder m_linkHolder; // 0x40
    };

    static_assert(sizeof(C_LinkableObjectExtension) == 0x58, "C_LinkableObjectExtension size mismatch");
} // namespace SDK::wh::xgenaimodule
