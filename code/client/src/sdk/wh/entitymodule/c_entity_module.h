#pragma once

#include "c_item_manager.h"
#include "c_item_slot_manager.h"
#include "c_weapon_system.h"
#include "../../patterns.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_InventoryManager;
    class C_StashManager;

    /**
     * Root entity-module service and owner of the item-slot spawning system.
     *
     * The module bootstrap allocates exactly 0x1D0 bytes and its constructor
     * at 0x180BE8968 publishes the completed object through the process-global
     * holder at 0x185487A60. Its stripped ten-slot virtual contract remains
     * opaque. C_ItemSlotManager is allocated during construction and retained
     * at +0xD8.
     *
     * The constructor also builds the inventory manager and stores it both at
     * +0xE8 and in the process-global the SDK reaches through
     * C_InventoryManager::GetInstance, which is what proves the two are the
     * same object. C_Stash::Init and C_Stash::Release read the stash manager
     * from +0xF0 and the schedule manager from +0x170.
     */
    class C_EntityModule {
      public:
        virtual ~C_EntityModule() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;

        static C_EntityModule *GetInstance() {
            return *reinterpret_cast<C_EntityModule **>(
                gPatterns.C_EntityModule__Instance);
        }

        /** Every live item, by WUID; built here and published at 0x185487A78. */
        C_ItemManager *GetItemManager() const {
            return m_pItemManager;
        }

        C_ItemSlotManager *GetItemSlotManager() {
            return m_pItemSlotManager;
        }
        const C_ItemSlotManager *GetItemSlotManager() const {
            return m_pItemSlotManager;
        }

        C_InventoryManager *GetInventoryManager() const {
            return m_pInventoryManager;
        }

        C_StashManager *GetStashManager() const {
            return m_pStashManager;
        }

        CWeaponSystem *GetWeaponSystem() const {
            return m_pWeaponSystem;
        }

        /**
         * The service every I_ScheduleUpdatable registers with, including the
         * one embedded in C_Stash at its +0x70. Left untyped because only its
         * register and unregister entry points have been established.
         */
        void *GetScheduleManager() const {
            return m_pScheduleManager;
        }

      private:
        // Ctor 0x180BE8968; the C_BaseModule base ctor 0x180BE90CC zeroes +0x08.
        std::uint32_t m_baseModule0008;          // 0x08, wh::framework::C_BaseModule state
        std::byte m_padding000C[0x4];            // 0x0C
        void *m_values0010[4];                   // 0x10, ctor zeroes
        std::byte m_opaque0030[0x8];             // 0x30
        void *m_values0038[7];                   // 0x38, ctor zeroes
        std::byte m_opaque0070[0x8];             // 0x70
        void *m_values0078[6];                   // 0x78, ctor zeroes
        void *m_pService00A8;                    // 0xA8, 0x10-byte object built by 0x180BE96C8
        void *m_pClothingAttachmentManagerFactory; // 0xB0, C_ClothingAttachmentManagerFactory (0x10 bytes)
        void *m_pClothingSystem;                 // 0xB8, C_ClothingSystem (0x1C8 bytes)
        std::byte m_opaque00C0[0x8];             // 0xC0
        C_ItemManager *m_pItemManager;           // 0xC8
        void *m_value00D0;                       // 0xD0, ctor zeroes
        C_ItemSlotManager *m_pItemSlotManager;   // 0xD8
        void *m_pService00E0;                    // 0xE0, 0x50-byte object built by 0x180BE8720
        C_InventoryManager *m_pInventoryManager; // 0xE8
        C_StashManager *m_pStashManager;         // 0xF0
        void *m_pService00F8;                    // 0xF8, 0x98-byte object
        void *m_pItemSlotSaveExtensions;         // 0x100, C_SaveExtensionManager<C_ItemSlot> (0x98 bytes)
        void *m_pSet0108;                        // 0x108, a heap-allocated native std::set<void *>
        void *m_value0110;                       // 0x110, ctor zeroes
        CWeaponSystem *m_pWeaponSystem;          // 0x118; Init allocates it at 0x181064736.
        void *m_value0120;                       // 0x120, ctor zeroes
        void *m_pService0128;                    // 0x128, 0x58-byte object built by 0x180BE9D40
        void *m_pService0130;                    // 0x130, 0x58-byte object built by 0x180BE9DD4
        void *m_pService0138;                    // 0x138, 0x10-byte object
        void *m_pService0140;                    // 0x140, 0x30-byte object built by 0x180BE90E4
        void *m_pService0148;                    // 0x148, 0x10-byte object built by 0x180BE8EA8
        void *m_pService0150;                    // 0x150, 0x20-byte object built by 0x180BE9108
        void *m_pService0158;                    // 0x158, 0x130-byte object built by 0x180BC7F80
        void *m_pService0160;                    // 0x160, 0xC8-byte object built by 0x180BE87E8
        void *m_pService0168;                    // 0x168, 0x10-byte object built by 0x180BE91B8
        void *m_pScheduleManager;                // 0x170, 0x58 bytes, built by 0x180BE8EBC
        void *m_values0178[2];                   // 0x178, ctor zeroes
        void *m_pService0188;                    // 0x188, 0x30-byte object built by 0x180BE9538
        void *m_pService0190;                    // 0x190, 0x70-byte object built by 0x180BE84C8
        void *m_pService0198;                    // 0x198, 0x8-byte object built by 0x180BE76D8
        void *m_pActorMonsterLODBudgetting;      // 0x1A0, C_ActorMonsterLODBudgetting (0x70 bytes)
        void *m_pWideStringMap;                  // 0x1A8, a heap-allocated native std::map<std::wstring, std::wstring>
        void *m_pService01B0;                    // 0x1B0, 0x40-byte object built by 0x180BE9134
        void *m_value01B8;                       // 0x1B8, ctor zeroes
        std::byte m_torchContextWatcher[0x10];   // 0x1C0, embedded C_TorchContextWatcher (vftable, then a zeroed pointer)

        static void AssertLayout() {
            static_assert(offsetof(C_EntityModule, m_pItemManager) == 0xC8, "C_EntityModule::m_pItemManager offset mismatch");
            static_assert(offsetof(C_EntityModule, m_pItemSlotManager) == 0xD8, "C_EntityModule::m_pItemSlotManager offset mismatch");
            static_assert(offsetof(C_EntityModule, m_pInventoryManager) == 0xE8, "C_EntityModule::m_pInventoryManager offset mismatch");
            static_assert(offsetof(C_EntityModule, m_pStashManager) == 0xF0, "C_EntityModule::m_pStashManager offset mismatch");
            static_assert(offsetof(C_EntityModule, m_pWeaponSystem) == 0x118, "C_EntityModule::m_pWeaponSystem offset mismatch");
            static_assert(offsetof(C_EntityModule, m_pScheduleManager) == 0x170, "C_EntityModule::m_pScheduleManager offset mismatch");
            static_assert(offsetof(C_EntityModule, m_baseModule0008) == 0x08, "C_EntityModule::m_baseModule0008 offset mismatch");
            static_assert(offsetof(C_EntityModule, m_pClothingSystem) == 0xB8, "C_EntityModule::m_pClothingSystem offset mismatch");
            static_assert(offsetof(C_EntityModule, m_pItemSlotSaveExtensions) == 0x100, "C_EntityModule::m_pItemSlotSaveExtensions offset mismatch");
            static_assert(offsetof(C_EntityModule, m_pActorMonsterLODBudgetting) == 0x1A0, "C_EntityModule::m_pActorMonsterLODBudgetting offset mismatch");
            static_assert(offsetof(C_EntityModule, m_torchContextWatcher) == 0x1C0, "C_EntityModule::m_torchContextWatcher offset mismatch");
        }
    };

    static_assert(sizeof(C_EntityModule) == 0x1D0,
                  "C_EntityModule size mismatch");
} // namespace SDK::wh::entitymodule
