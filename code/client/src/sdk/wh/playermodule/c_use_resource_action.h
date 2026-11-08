#pragma once

#include "c_alchemy_action_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /** Native animation callback receiver; borrowed only during its call. */
    class C_UseResourceAction: public SDK::wh::playermodule::C_AlchemyActionBase<SDK::wh::playermodule::S_EmptyParams, SDK::wh::framework::C_Action<SDK::wh::playermodule::I_AlchemyAction>> {
      private:
        // Constructor 0x1808D381C.
        std::uint32_t m_attachEventHash; // 0x68; lower-cased CRC32 of "Attach"
        std::uint32_t m_detachEventHash; // 0x6C; lower-cased CRC32 of "Detach"
        std::uint32_t m_useEventHash;    // 0x70; lower-cased CRC32 of the "Use" literal at 0x183A5C7D8
        std::byte m_padding0074[0x4];
        void *m_ptr0078;                 // 0x78; ctor 0

        static void AssertLayout() {
            static_assert(offsetof(C_UseResourceAction, m_attachEventHash) == 0x68);
            static_assert(offsetof(C_UseResourceAction, m_ptr0078) == 0x78);
        }
    };

    static_assert(sizeof(C_UseResourceAction) == 0x80);
} // namespace SDK::wh::playermodule
