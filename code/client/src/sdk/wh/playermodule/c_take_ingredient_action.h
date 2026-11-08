#pragma once

#include "c_alchemy_action_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /** Native animation callback receiver; borrowed only during its call. */
    class C_TakeIngredientAction: public SDK::wh::playermodule::C_AlchemyActionBase<SDK::wh::playermodule::S_EmptyParams, SDK::wh::framework::C_Action<SDK::wh::playermodule::I_AlchemyAction>> {
      private:
        // Constructor 0x1808D326C.
        std::uint32_t m_attachEventHash;    // 0x68; lower-cased CRC32 of "Attach"
        std::uint32_t m_actionEndEventHash; // 0x6C; lower-cased CRC32 of "ActionEnd"
        void *m_ptr0070;                    // 0x70; ctor 0

        static void AssertLayout() {
            static_assert(offsetof(C_TakeIngredientAction, m_attachEventHash) == 0x68);
            static_assert(offsetof(C_TakeIngredientAction, m_ptr0070) == 0x70);
        }
    };

    static_assert(sizeof(C_TakeIngredientAction) == 0x78);
} // namespace SDK::wh::playermodule
