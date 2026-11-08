#pragma once

#include "../../patterns.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace SDK::wh::entitymodule {
    class C_Item;

    // Slot zero is Clone, not a destructor.
    class I_ItemRuntimeData {
      public:
        virtual void OpaqueRuntime00();
        virtual void OpaqueRuntime01();
        virtual void SetQuality(std::uint32_t quality);
        virtual std::uint32_t GetQuality() const;
        virtual void OpaqueRuntime04();
        virtual void OpaqueRuntime05();
        virtual void OpaqueRuntime06();
        virtual void OpaqueRuntime07();
        virtual void OpaqueRuntime08();
        virtual void OpaqueRuntime09();
        virtual void OpaqueRuntime10();
        virtual void OpaqueRuntime11();
        virtual void OpaqueRuntime12();
        virtual void OpaqueRuntime13();
        virtual void OpaqueRuntime14();
        virtual void OpaqueRuntime15();
        virtual void OpaqueRuntime16();
        virtual void OpaqueRuntime17();
        virtual void OpaqueRuntime18();
        virtual void OpaqueRuntime19();
        virtual void OpaqueRuntime20();
        virtual void OpaqueRuntime21();
        virtual void OpaqueRuntime22();
        virtual void OpaqueRuntime23();

      private:
        // Ctor 0x18046FBF0.
        std::byte m_opaque0008[0x10];
        std::uint8_t m_state0018;    // 0x18, ctor zeroes
        std::byte m_padding0019[0x7];
        void *m_pLink0020;           // 0x20, ctor points it at +0x08
        void *m_pLink0028;           // 0x28, ctor points it at +0x08
        void *m_values0030[3];       // 0x30, ctor zeroes
        C_Item *m_pItem;             // 0x48, the owning item the ctor is given
        void *m_value0050;           // 0x50, ctor zeroes

        static void AssertLayout() {
            static_assert(offsetof(I_ItemRuntimeData, m_state0018) == 0x18, "I_ItemRuntimeData::m_state0018 offset mismatch");
            static_assert(offsetof(I_ItemRuntimeData, m_pLink0020) == 0x20, "I_ItemRuntimeData::m_pLink0020 offset mismatch");
            static_assert(offsetof(I_ItemRuntimeData, m_pItem) == 0x48, "I_ItemRuntimeData::m_pItem offset mismatch");
        }
    };

    static_assert(sizeof(I_ItemRuntimeData) == 0x58, "I_ItemRuntimeData size mismatch");

    class C_EquippableItemRuntimeData: public I_ItemRuntimeData {
      public:
        virtual void OpaqueRuntime24();
        virtual void OpaqueRuntime25();
        virtual void OpaqueRuntime26();
        virtual void OpaqueRuntime27();
        virtual void OpaqueRuntime28();
        virtual void OpaqueRuntime29();
        virtual void OpaqueRuntime30();

        std::size_t GetOnEquipBuffCount() const {
            return m_onEquipBuffCount;
        }

        // Borrowed in-order tree access. Neither nodes nor the sentinel may
        // be released through the mod's allocator. The bound also rejects a
        // broken link instead of following a native cycle indefinitely.
        bool GetOnEquipBuffAt(std::size_t index, SDK::CryGUID &result) const {
            if (index >= m_onEquipBuffCount || m_onEquipBuffHead == nullptr) {
                return false;
            }
            const OnEquipBuffNode *node = m_onEquipBuffHead->left;
            for (std::size_t current = 0; current < index; ++current) {
                std::size_t traversed = 0;
                if (node == nullptr || node->nil != 0 || ++traversed > m_onEquipBuffCount) {
                    return false;
                }
                if (node->right == nullptr) {
                    return false;
                }
                if (node->right->nil == 0) {
                    node = node->right;
                    while (node->left != nullptr && node->left->nil == 0) {
                        if (++traversed > m_onEquipBuffCount) {
                            return false;
                        }
                        node = node->left;
                    }
                }
                else {
                    const OnEquipBuffNode *parent = node->parent;
                    while (parent != nullptr && parent->nil == 0 && node == parent->right) {
                        if (++traversed > m_onEquipBuffCount) {
                            return false;
                        }
                        node   = parent;
                        parent = parent->parent;
                    }
                    node = parent;
                }
            }
            if (node == nullptr || node->nil != 0) {
                return false;
            }
            std::memcpy(&result, node->guid, sizeof(result));
            return true;
        }

        void AddOnEquipBuff(const SDK::CryGUID &buff) {
            alignas(16) const SDK::CryGUID aligned = buff;
            hook::this_call<void>(gPatterns.C_EquippableItemRuntimeData__AddOnEquipBuffRaw, this, &aligned);
        }

        void RemoveOnEquipBuff(const SDK::CryGUID &buff) {
            // Native RemoveOnEquipBuffRaw starts with MOVAPS [RDX].
            alignas(16) const SDK::CryGUID aligned = buff;
            hook::this_call<void>(gPatterns.C_EquippableItemRuntimeData__RemoveOnEquipBuffRaw, this, &aligned);
        }

      private:
        // A layout view of the native ordered-set node, not a new engine
        // ownership type. Constructor1809445DC stores the GUID at+1C.
        struct OnEquipBuffNode {
            OnEquipBuffNode *left;
            OnEquipBuffNode *parent;
            OnEquipBuffNode *right;
            std::uint8_t color;
            std::uint8_t nil;
            std::byte m_opaque001A[0x2];
            std::byte guid[0x10];
            std::byte m_opaque002C[0x4];
        };

        // Ctor 0x18046FB14.
        std::byte m_opaque0058[0x8];
        std::uint8_t m_state0060;           // 0x60, ctor zeroes
        std::byte m_padding0061[0x7];
        void *m_pLink0068;                  // 0x68, ctor points it at +0x58
        void *m_pLink0070;                  // 0x70, ctor points it at +0x58
        void *m_values0078[3];              // 0x78, ctor zeroes
        OnEquipBuffNode *m_onEquipBuffHead; // 0x90
        std::size_t m_onEquipBuffCount;     // 0x98
        std::uint32_t m_value00A0;          // 0xA0, ctor reads it from the item descriptor's vtable slot 6 (1 without an item)
        float m_value00A4;                  // 0xA4, ctor sets 1.0

        static void AssertLayout() {
            static_assert(sizeof(OnEquipBuffNode) == 0x30);
            static_assert(offsetof(OnEquipBuffNode, left) == 0x0);
            static_assert(offsetof(OnEquipBuffNode, parent) == 0x8);
            static_assert(offsetof(OnEquipBuffNode, right) == 0x10);
            static_assert(offsetof(OnEquipBuffNode, color) == 0x18);
            static_assert(offsetof(OnEquipBuffNode, nil) == 0x19);
            static_assert(offsetof(OnEquipBuffNode, guid) == 0x1C);
            static_assert(offsetof(C_EquippableItemRuntimeData, m_onEquipBuffHead) == 0x90);
            static_assert(offsetof(C_EquippableItemRuntimeData, m_onEquipBuffCount) == 0x98);
            static_assert(offsetof(C_EquippableItemRuntimeData, m_pLink0068) == 0x68);
            static_assert(offsetof(C_EquippableItemRuntimeData, m_value00A0) == 0xA0);
        }
    };

    static_assert(sizeof(C_EquippableItemRuntimeData) == 0xA8, "C_EquippableItemRuntimeData size mismatch");

    class C_WeaponEquipRuntimeData: public C_EquippableItemRuntimeData {
      public:
        const CryGUID &GetPoisonBuff() const {
            return m_poisonBuff;
        }

        std::uint32_t GetPoisonCharges() const {
            return m_poisonCharges;
        }

        void SetPoison(const CryGUID &buff, std::uint32_t charges) {
            hook::this_call<void>(gPatterns.C_WeaponEquipRuntimeData__SetPoison, this, &buff, charges);
        }

      private:
        CryGUID m_poisonBuff;          // 0xA8; WeaponBuffDefinitionId
        std::uint32_t m_poisonCharges; // 0xB8
        std::byte m_padding00BC[0x4]; // ctor 0x1804744C0 stops at the dword at 0xB8

        static void AssertLayout() {
            static_assert(offsetof(C_WeaponEquipRuntimeData, m_poisonBuff) == 0xA8);
            static_assert(offsetof(C_WeaponEquipRuntimeData, m_poisonCharges) == 0xB8);
        }
    };

    static_assert(sizeof(C_WeaponEquipRuntimeData) == 0xC0, "C_WeaponEquipRuntimeData size mismatch");

    class C_AmmoRuntimeData: public C_WeaponEquipRuntimeData {
      private:
        std::uint32_t m_value00C0; // 0xC0, CreateRuntimeData 0x180F19534 zeroes
        std::byte m_padding00C4[0x4];
    };

    static_assert(sizeof(C_AmmoRuntimeData) == 0xC8, "C_AmmoRuntimeData size mismatch");

    class C_WeaponRuntimeData: public C_WeaponEquipRuntimeData {
      private:
        // Ctor 0x180474450.
        std::byte m_opaque00C0[0x4];
        std::uint32_t m_value00C4; // 0xC4, ctor zeroes
        void *m_value00C8;         // 0xC8, ctor zeroes
        void *m_value00D0;         // 0xD0, ctor zeroes
    };

    static_assert(sizeof(C_WeaponRuntimeData) == 0xD8, "C_WeaponRuntimeData size mismatch");

    class C_MissileWeaponRuntimeData: public C_WeaponRuntimeData {
      public:
        bool IsCocked() const {
            return m_cocked;
        }

        bool HasLoadedAmmunition() const {
            return m_loadedAmmunitionWuid != UINT64_MAX;
        }

        std::uint64_t GetLoadedAmmunitionWuid() const {
            return m_loadedAmmunitionWuid;
        }

        // Native setters update reservations and attachments together.
        void SetCocked(bool cocked) {
            hook::this_call<void>(gPatterns.C_MissileWeaponRuntimeData__SetCocked, this, cocked);
        }

        void LoadAmmunition(C_Item *item) {
            hook::this_call<void>(gPatterns.C_MissileWeaponRuntimeData__LoadAmmunition, this, item);
        }

        void UnloadAmmunition() {
            hook::this_call<void>(gPatterns.C_MissileWeaponRuntimeData__UnloadAmmunition, this);
        }

      private:
        bool m_cocked; // 0xD8
        std::uint8_t m_state00D9; // 0xD9, CreateRuntimeData_180F196C8 zeroes it with m_cocked
        std::byte m_padding00DA[0x6]; // CreateRuntimeData_180F196C8 writes the word at 0xD8, then the qword at 0xE0
        std::uint64_t m_loadedAmmunitionWuid; // 0xE0
        std::byte m_opaque00E8[0x10];

        static void AssertLayout() {
            static_assert(offsetof(C_MissileWeaponRuntimeData, m_cocked) == 0xD8, "C_MissileWeaponRuntimeData::m_cocked offset mismatch");
            static_assert(offsetof(C_MissileWeaponRuntimeData, m_loadedAmmunitionWuid) == 0xE0, "C_MissileWeaponRuntimeData::m_loadedAmmunitionWuid offset mismatch");
        }
    };

    static_assert(sizeof(C_MissileWeaponRuntimeData) == 0xF8, "C_MissileWeaponRuntimeData size mismatch");
} // namespace SDK::wh::entitymodule
