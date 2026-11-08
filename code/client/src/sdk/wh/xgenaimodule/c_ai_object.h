#pragma once

#include "../../types.h"
#include "../framework/wuid.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::xgenaimodule {
    /**
     * Whatever an AI object stands for in the world.
     *
     * Only slot 1 is proven: `C_ScriptBindXGenAIModule::GetEntityIdByWUID`
     * looks a WUID up to a `C_AIObject`, takes this from its `+0x18` and
     * returns what slot 1 answers as the entity id. Which class implements it
     * is not recovered, so the name says what it is for rather than what it is.
     */
    class I_AIObjectEntityOwner {
      public:
        virtual void unk_fn0000()                 = 0;
        virtual SDK::EntityId GetEntityId() const = 0; // slot 1, +0x08
    };

    /**
     * The XGenAI record a WUID names.
     *
     * The constructor `C_AIObject(const WUID &, owner)` (0x18047BDFC) stores
     * the WUID at `+0x08` and the owner at `+0x18`, and registers itself in
     * `C_AIObjectManager`'s map under that WUID. Never constructed by the mod;
     * the tail is not modeled, so only the prefix is asserted.
     */
    class C_AIObject {
      public:
        virtual ~C_AIObject() = default;

        framework::WUID GetWuid() const {
            return m_wuid;
        }

        I_AIObjectEntityOwner *GetOwner() const {
            return m_pOwner;
        }

      private:
        framework::WUID m_wuid;          // 0x08
        std::int32_t m_state0010;        // 0x10, the ctor (0x18047BDFC) sets -1
        std::int32_t m_state0014;        // 0x14, the ctor sets -1
        I_AIObjectEntityOwner *m_pOwner; // 0x18

        static void AssertLayout() {
            static_assert(offsetof(C_AIObject, m_wuid) == 0x08, "C_AIObject::m_wuid offset mismatch");
            static_assert(offsetof(C_AIObject, m_pOwner) == 0x18, "C_AIObject::m_pOwner offset mismatch");
        }
    };
} // namespace SDK::wh::xgenaimodule
