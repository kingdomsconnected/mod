#pragma once

#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Owned by `C_CombatModule +0x30`, created by its Init (vtable slot 2,
     * 0x180D243C0) with a 0x110-byte allocation and constructor 0x180D24624,
     * which installs C_BattleManager RTTI tables at +0x00/+0x08/+0x10/+0x18
     * (four bases; +0x10 is registered as a listener with the game modules).
     * Released through its scalar deleting destructor in the module's
     * Shutdown (slot 3, 0x182778C10).
     */
    class C_BattleManager {
      private:
        const void *m_pVTables0000[4];   // 0x00; one per base
        shared::C_Signal m_signal0020;   // 0x20; dispatch unk_185666C38
        shared::C_Signal m_signal0030;   // 0x30; dispatch unk_185666C48
        shared::C_Signal m_signal0040;   // 0x40; dispatch unk_185666C58
        void *m_ptrs0050[16];            // 0x50; ctor 0
        std::uint8_t m_flag00D0;         // 0xD0; ctor 1
        std::byte m_padding00D1[0x7];
        void *m_pOwned00D8;              // 0xD8; owning pointer set from sub_180D253C0
        void *m_pSequenceListener;       // 0xE0; 0x10-byte C_BattleSequenceEventListener {vftable, this}
        void *m_pVector00E8;             // 0xE8; 0x20-byte {begin, end, capacity, this}
        void *m_ptrs00F0[3];             // 0xF0; ctor 0
        std::int32_t m_value0108;        // 0x108; ctor -1
        std::uint16_t m_value010C;       // 0x10C; ctor 0
        std::byte m_padding010E[0x2];

        static void AssertLayout() {
            static_assert(offsetof(C_BattleManager, m_signal0020) == 0x20, "C_BattleManager::m_signal0020 offset mismatch");
            static_assert(offsetof(C_BattleManager, m_flag00D0) == 0xD0, "C_BattleManager::m_flag00D0 offset mismatch");
            static_assert(offsetof(C_BattleManager, m_pSequenceListener) == 0xE0, "C_BattleManager::m_pSequenceListener offset mismatch");
            static_assert(offsetof(C_BattleManager, m_value0108) == 0x108, "C_BattleManager::m_value0108 offset mismatch");
        }
    };

    static_assert(sizeof(C_BattleManager) == 0x110, "C_BattleManager size mismatch");
} // namespace SDK::wh::combatmodule
