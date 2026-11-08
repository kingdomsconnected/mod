#pragma once

#include "c_alchemy_action_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /** RTTI 0x183B87A38; factory 0x18143163C allocates 0x78, ctor 0x1814316E4. */
    class C_ReturnIngredientAction: public SDK::wh::playermodule::C_AlchemyActionBase<SDK::wh::playermodule::S_EmptyParams, SDK::wh::framework::C_Action<SDK::wh::playermodule::I_AlchemyAction>> {
      private:
        // Constructor 0x1814316E4.
        std::uint32_t m_detachEventHash; // 0x68; lower-cased CRC32 of "Detach"
        std::byte m_padding006C[0x4];
        void *m_ptr0070;                 // 0x70; ctor 0

        static void AssertLayout() {
            static_assert(offsetof(C_ReturnIngredientAction, m_detachEventHash) == 0x68);
            static_assert(offsetof(C_ReturnIngredientAction, m_ptr0070) == 0x70);
        }
    };

    static_assert(sizeof(C_ReturnIngredientAction) == 0x78);
} // namespace SDK::wh::playermodule
