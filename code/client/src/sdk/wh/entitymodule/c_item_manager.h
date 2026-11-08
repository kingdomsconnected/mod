#pragma once

#include "../../patterns.h"
#include "../framework/wuid.h"
#include "../shared/c_signal.h"
#include "c_item.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Every live item, whoever holds it.
     *
     * `C_EntityModule`'s constructor (0x180BE8968) allocates 0x5B9070 bytes
     * for it, builds it with the vtable RTTI names `C_ItemManager`, stores it
     * at the module's +0xC8 and copies it to the process-global the game's
     * own lookups read (`C_ItemManager::s_pInstance`, 0x185487A78). Only the
     * lookup below is recovered; its ten-slot interface is not.
     */
    class C_ItemManager {
      public:
        virtual void unk_fn0000() = 0;

        /**
         * The item with this id, or null. Holder-agnostic: answers for
         * player, NPC, world and pile items alike -- and for an item the
         * carry action holds, which no inventory does.
         */
        C_Item *FindItem(framework::WUID itemId) const {
            return hook::this_call<C_Item *>(gPatterns.C_ItemManager__FindItem, this, itemId);
        }

      private:
        // Ctor 0x180BEAB68. The three 0x58-byte runs are a signal dispatch
        // followed by forty 16-bit subscriber handles, all set to -1.
        SDK::wh::shared::C_Signal m_signal0008;  // 0x08, dispatch 0x1856686C8
        SDK::wh::shared::C_Signal m_signal0018;  // 0x18, built by 0x1816F6A0C, dispatch 0x1856686B8
        const void *m_pDispatch0028;             // 0x28, 0x185667108
        std::uint16_t m_handles0030[40];         // 0x30
        std::byte m_padding0080[0x8];
        const void *m_pDispatch0088;             // 0x88, 0x185667108
        std::uint16_t m_handles0090[40];         // 0x90
        std::byte m_padding00E0[0x8];
        SDK::wh::shared::C_Signal m_signal00E8;  // 0xE8, dispatch 0x185667108
        const void *m_pDispatch00F8;             // 0xF8, 0x185668538
        std::uint16_t m_handles0100[40];         // 0x100
        std::byte m_opaque0150[0x18];
        std::byte m_itemsById[0x10]; // 0x168, the slot map FindItem reads; 0x181F06920 builds the store from here
        std::byte m_opaque0178[0x5B8DD8];
        void *m_values5B8F50[3];                 // 0x5B8F50, ctor zeroes
        std::byte m_hashMap5B8F68[0x40];         // 0x5B8F68, native std::unordered_map (load factor 1.0, 8 buckets)
        std::byte m_state5B8FA8[0x40];           // 0x5B8FA8, built by 0x1806E4638
        std::byte m_state5B8FE8[0x40];           // 0x5B8FE8, built by 0x18099BDDC
        std::uint64_t m_value5B9028;             // 0x5B9028, ctor zeroes
        std::byte m_tree5B9030[0x10];            // 0x5B9030, native std::map/set {head, size}; 0x30-byte head node
        std::uint64_t m_value5B9040;             // 0x5B9040, ctor zeroes
        std::byte m_tree5B9048[0x10];            // 0x5B9048, native std::map/set {head, size}; 0x50-byte head node
        std::uint64_t m_value5B9058;             // 0x5B9058, ctor zeroes
        std::uint64_t m_value5B9060;             // 0x5B9060, ctor zeroes
        std::uint32_t m_value5B9068;             // 0x5B9068, ctor zeroes
        bool m_state5B906C;                      // 0x5B906C, ctor clears
        std::byte m_padding5B906D[0x3];

        static void AssertLayout() {
            static_assert(offsetof(C_ItemManager, m_itemsById) == 0x168, "C_ItemManager::m_itemsById offset mismatch");
            static_assert(offsetof(C_ItemManager, m_signal0008) == 0x08, "C_ItemManager::m_signal0008 offset mismatch");
            static_assert(offsetof(C_ItemManager, m_handles0030) == 0x30, "C_ItemManager::m_handles0030 offset mismatch");
            static_assert(offsetof(C_ItemManager, m_signal00E8) == 0xE8, "C_ItemManager::m_signal00E8 offset mismatch");
            static_assert(offsetof(C_ItemManager, m_handles0100) == 0x100, "C_ItemManager::m_handles0100 offset mismatch");
            static_assert(offsetof(C_ItemManager, m_values5B8F50) == 0x5B8F50, "C_ItemManager::m_values5B8F50 offset mismatch");
            static_assert(offsetof(C_ItemManager, m_hashMap5B8F68) == 0x5B8F68, "C_ItemManager::m_hashMap5B8F68 offset mismatch");
            static_assert(offsetof(C_ItemManager, m_signal0018) == 0x18, "C_ItemManager::m_signal0018 offset mismatch");
            static_assert(offsetof(C_ItemManager, m_tree5B9030) == 0x5B9030, "C_ItemManager::m_tree5B9030 offset mismatch");
            static_assert(offsetof(C_ItemManager, m_state5B906C) == 0x5B906C, "C_ItemManager::m_state5B906C offset mismatch");
        }
    };

    static_assert(sizeof(C_ItemManager) == 0x5B9070, "C_ItemManager size mismatch");
} // namespace SDK::wh::entitymodule
