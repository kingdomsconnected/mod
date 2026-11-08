#pragma once

#include "c_buff_instance.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * The intermediate base every soul buff implementation derives from.
     *
     * RTTI chain and constructor 0x18046C4DC prove it. Its modifier
     * collections and flags precede every timed instance's own fields.
     */
    class C_SoulBuffInstance: public C_BuffInstanceBase {
      private:
        // Constructor sub_18046C4DC.
        void *m_ptrs0090[8];          // 0x90; ctor 0
        std::uint16_t m_value00D0;    // 0xD0; ctor 0
        std::byte m_padding00D2[0x6];

        static void AssertLayout() {
            static_assert(offsetof(C_SoulBuffInstance, m_ptrs0090) == 0x90, "C_SoulBuffInstance::m_ptrs0090 offset mismatch");
            static_assert(offsetof(C_SoulBuffInstance, m_value00D0) == 0xD0, "C_SoulBuffInstance::m_value00D0 offset mismatch");
        }
    };
    static_assert(sizeof(C_SoulBuffInstance) == 0xD8, "C_SoulBuffInstance size mismatch");
} // namespace SDK::wh::rpgmodule
