#pragma once

#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Owned by `C_CombatModule +0x38`, created by its Init (vtable slot 2,
     * 0x180D243C0) with a 0x90-byte allocation and constructor 0x180D24894,
     * which installs the C_DeadBodyManager RTTI table at +0x00 and +0x08
     * (two bases). Released through its vtable slot 2 in the module's
     * Shutdown (slot 3, 0x182778C10).
     */
    class C_DeadBodyManager {
      private:
        const void *m_pVTable0000;   // 0x00
        const void *m_pVTable0008;   // 0x08; second base
        shared::C_Signal m_signal0010; // 0x10; dispatch unk_185666C58
        void *m_ptrs0020[12];        // 0x20; ctor 0
        void *m_pOwned0080;          // 0x80; owning pointer set from sub_180D253C0
        std::uint8_t m_flag0088;     // 0x88; ctor 0
        std::byte m_padding0089[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_DeadBodyManager, m_signal0010) == 0x10, "C_DeadBodyManager::m_signal0010 offset mismatch");
            static_assert(offsetof(C_DeadBodyManager, m_pOwned0080) == 0x80, "C_DeadBodyManager::m_pOwned0080 offset mismatch");
            static_assert(offsetof(C_DeadBodyManager, m_flag0088) == 0x88, "C_DeadBodyManager::m_flag0088 offset mismatch");
        }
    };

    static_assert(sizeof(C_DeadBodyManager) == 0x90, "C_DeadBodyManager size mismatch");
} // namespace SDK::wh::combatmodule
