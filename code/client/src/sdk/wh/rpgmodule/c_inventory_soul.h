#pragma once

#include "../entitymodule/i_inventory_listener.h"
#include "../framework/i_schedule_updatable.h"
#include "i_inventory_soul.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    class C_PlayerInventorySoul;
}

namespace SDK::wh::entitymodule {
    class C_EquipmentManager;
    class C_Inventory;
} // namespace SDK::wh::entitymodule

namespace SDK::wh::rpgmodule {
    class I_Soul;
    /** Core inventory runtime embedded at C_Soul+0x198. */
    class C_InventorySoul
        : public I_InventorySoul
        , public entitymodule::I_InventoryListener
        , public framework::I_ScheduleUpdatable {
      public:
        // One concrete implementation serves the identically-shaped callback
        // slots contributed by the secondary listener/scheduler bases too.
        void unk_fn0000() override;
        void unk_fn0001() override;
        void unk_fn0002() override; // I_InventoryListener's slot of that name
        void ResetInventory(bool force, bool createAll) override;
        void unk_fn0003() override;
        void unk_fn0004() override;
        void unk_fn0005() override;
        void unk_fn0006() override;
        void unk_fn0007() override;
        void unk_fn0008() override;
        void unk_fn0009() override;
        void PushClothingPreset(S_PresetOverride preset, E_PresetItemMode itemMode, bool applyNow) override;
        void PushWeaponPreset(S_PresetOverride preset, E_PresetItemMode itemMode, bool applyNow) override;
        void RemovePreset(S_PresetOverride preset, E_PresetItemMode itemMode, int weaponStack, bool applyNow, bool skipCreate) override;
        void unk_fn0013() override;
        void unk_fn0014() override;
        void unk_fn0015() override;
        void unk_fn0016() override;
        void unk_fn0017() override;
        void unk_fn0018() override;
        void unk_fn0019() override;
        void unk_fn0020() override;
        void unk_fn0021() override;
        void unk_fn0022() override;
        void unk_fn0023() override;
        void unk_fn0024() override;
        void unk_fn0025() override;
        void unk_fn0026() override;
        void unk_fn0027() override;
        void unk_fn0028() override;
        void unk_fn0029() override;
        void unk_fn0030() override;
        void unk_fn0031() override;
        void unk_fn0032() override;
        void unk_fn0033() override;
        void unk_fn0034() override;
        void unk_fn0035() override;
        void unk_fn0036() override;
        void unk_fn0037() override;
        void unk_fn0038() override;
        void unk_fn0039() override;
        void unk_fn0040() override;
        void unk_fn0041() override;
        void unk_fn0042() override;
        const CryGUID *GetClothingPreset() const override;
        const CryGUID *GetWeaponPreset() const override;
        void unk_fn0045() override;
        void unk_fn0046() override;
        void unk_fn0047() override;
        void unk_fn0048() override;
        void unk_fn0049() override;
        void unk_fn0050() override;
        void unk_fn0051() override;

        playermodule::C_PlayerInventorySoul *GetPlayerInventorySoul() const {
            return m_pPlayerInventorySoul;
        }

        I_Soul *GetOwningSoul() const {
            return m_pOwningSoul;
        }

        /**
         * True when this runtime has been initialised for `soul`.
         *
         * Everything but the vtables starts as uninitialised memory, and the
         * inventory and equipment members read back as plausible pointers
         * rather than null, so neither can vouch for itself.
         */
        bool IsInitialisedFor(const I_Soul *soul) const {
            return m_pOwningSoul == soul;
        }

        entitymodule::C_Inventory *GetInventory() const {
            return m_pInventory;
        }
        entitymodule::C_EquipmentManager *GetEquipmentManager() const {
            return m_pEquipmentManager;
        }

      private:
        // The soul this runtime belongs to. Read back by the construction
        // path for the identifiers the inventory is keyed on, and the only
        // member here whose correct value is known independently, which is
        // what makes IsInitialisedFor a usable test.
        rpgmodule::I_Soul *m_pOwningSoul;                            // 0x18
        // Constructor sub_1803F22D8.
        void *m_ptrs0020[6];                                         // 0x20; ctor 0
        std::byte m_constants0050[0x20];                             // 0x50; ctor copies xmmword_183A33718 and xmmword_183A33728
        void *m_pEmptyString0070;                                    // 0x70; sub_1804FD80C() + 3, the shared empty-string rep
        void *m_ptr0078;                                             // 0x78; ctor copies qword_185332350
        void *m_ptrs0080[18];                                        // 0x80; ctor 0
        playermodule::C_PlayerInventorySoul *m_pPlayerInventorySoul; // 0x110
        entitymodule::C_Inventory *m_pInventory;                     // 0x118
        entitymodule::C_EquipmentManager *m_pEquipmentManager;       // 0x120
        void *m_pState0128;                                          // 0x128
        void *m_pState0130;                                          // 0x130
        std::int32_t m_value0138;                                    // 0x138; ctor 0
        std::int32_t m_value013C;                                    // 0x13C; ctor -1
        std::uint16_t m_value0140;                                   // 0x140; ctor copies RPG params word 1288
        std::uint16_t m_value0142;                                   // 0x142; same value
        std::uint8_t m_flag0144;                                     // 0x144; ctor 0
        std::uint8_t m_bits0145;                                     // 0x145; ctor clears the low five bits
        std::uint8_t m_flag0146;                                     // 0x146; ctor 0
        std::byte m_padding0147[0x1];

        static void AssertLayout() {
            static_assert(offsetof(C_InventorySoul, m_pOwningSoul) == 0x18, "C_InventorySoul::m_pOwningSoul offset mismatch");
            static_assert(offsetof(C_InventorySoul, m_pPlayerInventorySoul) == 0x110, "C_InventorySoul::m_pPlayerInventorySoul offset mismatch");
            static_assert(offsetof(C_InventorySoul, m_pInventory) == 0x118, "C_InventorySoul::m_pInventory offset mismatch");
            static_assert(offsetof(C_InventorySoul, m_pEquipmentManager) == 0x120, "C_InventorySoul::m_pEquipmentManager offset mismatch");
            static_assert(offsetof(C_InventorySoul, m_pState0128) == 0x128, "C_InventorySoul::m_pState0128 offset mismatch");
            static_assert(offsetof(C_InventorySoul, m_pState0130) == 0x130, "C_InventorySoul::m_pState0130 offset mismatch");
            static_assert(offsetof(C_InventorySoul, m_constants0050) == 0x50, "C_InventorySoul::m_constants0050 offset mismatch");
            static_assert(offsetof(C_InventorySoul, m_ptrs0080) == 0x80, "C_InventorySoul::m_ptrs0080 offset mismatch");
            static_assert(offsetof(C_InventorySoul, m_value0138) == 0x138, "C_InventorySoul::m_value0138 offset mismatch");
            static_assert(offsetof(C_InventorySoul, m_flag0144) == 0x144, "C_InventorySoul::m_flag0144 offset mismatch");
        }
    };

    static_assert(sizeof(C_InventorySoul) == 0x148, "C_InventorySoul size mismatch");
} // namespace SDK::wh::rpgmodule
