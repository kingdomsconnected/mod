#pragma once

#include "i_module_message_listener.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::framework {
    /** Common data prefix carried by Warhorse gameplay modules. */
    class C_BaseModule : public I_ModuleMessageListener {
      private:
        std::uint32_t m_state; // 0x08
        std::byte m_padding000C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_BaseModule, m_state) == 0x8, "C_BaseModule::m_state offset mismatch");
        }
    };

    static_assert(sizeof(C_BaseModule) == 0x10, "C_BaseModule size mismatch");
} // namespace SDK::wh::framework
