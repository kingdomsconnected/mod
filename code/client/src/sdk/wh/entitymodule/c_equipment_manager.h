#pragma once

#include "../../patterns.h"

#include "../framework/wuid.h"
#include "c_item.h"
#include "e_weapon_equip_slot.h"
#include "i_item_collection_listener.h"
#include "s_equipment_slot_id_wrapper.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace SDK::wh::rpgmodule {
    class C_Soul;
}

namespace SDK::wh::entitymodule {
    struct EquipmentItemVectorLayout {
        C_Item **m_pBegin;
        C_Item **m_pEnd;
        C_Item **m_pCapacityEnd;
    };

    struct EquipmentBitVectorLayout {
        std::uint32_t *m_pBegin;
        std::uint32_t *m_pEnd;
        std::uint32_t *m_pCapacityEnd;
        std::uint64_t m_bitCount;
    };

    static_assert(sizeof(EquipmentItemVectorLayout) == 0x18,
                  "EquipmentItemVectorLayout size mismatch");
    static_assert(sizeof(EquipmentBitVectorLayout) == 0x20,
                  "EquipmentBitVectorLayout size mismatch");

    /**
     * Equipment and quick-slot state owned by an RPG soul.
     *
     * C_EquipmentManager's table at 0x183EB1708 contains the six inherited
     * I_ItemCollectionListener entries plus three opaque manager entries.
     * Its owner construction path allocates exactly 0x130 bytes.
     */
    class C_EquipmentManager : public I_ItemCollectionListener {
      public:
        struct E_QuickSlotSpec {
            enum Type : int {
                QuickSlot1 = 0,
                QuickSlot2 = 1,
                QuickSlot3 = 2,
                QuickSlot4 = 3,
                QuickSlotMatchingWeaponSet = 4,
                Default = 5,
            };
        };

        /** Reflected 32-bit policy type; native enumerator names are stripped. */
        enum E_EquipPolicy : int {};

        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;

        /**
         * Puts one item on this body, and takes one off.
         *
         * Not the `Players` pair below, which cannot serve a body that is not
         * the player's: their shared worker wraps everything in a test of the
         * soul's own "is the player" flag -- bit 8 of the soul flags, set only
         * for the actor the game considers the player -- and returns having
         * silently done nothing when it fails. These two are what the engine
         * reaches for otherwise: the equip is where the quick-slot dispatcher
         * ends up once it has decided an item is plain armor, and the unequip
         * is what UnequipAllArmor drains the worn list with.
         *
         * `reason` and `blame` also control the unequipped item's destination.
         * Reason zero removes it from the inventory; it is not a neutral
         * keep-in-inventory policy. Reason one with an invalid destination
         * retains ownership (destination worker 0x180478DB4). For the local player, use the Players
         * pair below to
         * retain ownership and update the player's equipment.
         */
        bool InvokeEquipItemRaw(C_Item *item, bool notifyListeners = true) {
            return hook::this_call<bool>(gPatterns.C_EquipmentManager__EquipItemRaw, this, item, notifyListeners);
        }

        void InvokeUnequipItemRaw(C_Item *item, std::uint32_t reason = 0, std::uint64_t blame = 0, bool notifyListeners = true) {
            hook::this_call<void>(gPatterns.C_EquipmentManager__UnequipItemRaw, this, item, reason, blame, notifyListeners);
        }

        /**
         * Clears a weapon slot and its attachments, retaining inventory
         * ownership. EquipItemRaw uses this policy and invalid destination
         * when replacing a slot occupant (0x1808F8EE8).
         */
        void UnequipWeapon(C_Item *item) {
            hook::this_call<void>(gPatterns.C_EquipmentManager__ClearWeaponSlotItem, this, item, static_cast<E_EquipPolicy>(1), framework::WUID {framework::WUID::kInvalid});
        }

        /** Invokes the reflected native `EquipPlayersItem` member. */
        void InvokeEquipPlayersItem(
            std::shared_ptr<I_ItemDescriptor> itemDescriptor,
            E_QuickSlotSpec::Type equipToSlot = E_QuickSlotSpec::Default) {
            hook::this_call<void>(gPatterns.C_EquipmentManager__EquipPlayersItem, this, std::move(itemDescriptor), equipToSlot);
        }

        /** Invokes the reflected native `UnequipPlayersItem` member. */
        void InvokeUnequipPlayersItem(
            std::shared_ptr<I_ItemDescriptor> itemDescriptor) {
            hook::this_call<void>(gPatterns.C_EquipmentManager__UnequipPlayersItem, this, std::move(itemDescriptor));
        }

        /** Invokes the reflected native `UnequipPlayersArmorSlots` member. */
        void InvokeUnequipPlayersArmorSlots(
            const std::vector<S_EquipmentSlotIdWrapper> &armorSlots) {
            hook::this_call<void>(gPatterns.C_EquipmentManager__UnequipPlayersArmorSlots, this, armorSlots);
        }

        /**
         * Invokes the reflected native `UnequipAllArmor` member.
         *
         * RTTR proves zero-valued defaults for both policy and destInventoryId;
         * the policy enumerator labels themselves are not recoverable.
         */
        void InvokeUnequipAllArmor(
            E_EquipPolicy policy = static_cast<E_EquipPolicy>(0),
            framework::WUID destInventoryId = {}) {
            hook::this_call<void>(gPatterns.C_EquipmentManager__UnequipAllArmor, this, policy, destInventoryId);
        }

        rpgmodule::C_Soul *GetSoul() const { return m_pSoul; }

        /**
         * The worn helmet's VisorTypeId, or zero without a visor.
         * C_Player::RefreshHelmetActionHint calls 0x181F1EEE0, which walks
         * the equipped items and reads the helmet descriptor's +0x118 field.
         * RTTR names it VisorTypeId (0x18013C3F0). The native leaves the
         * output untouched without a helmet, so initialise it here.
         */
        std::uint32_t GetEquippedVisorTypeId() const {
            std::uint32_t type = 0;
            hook::this_call<void>(gPatterns.C_EquipmentManager__ReadEquippedVisorTypeId, this, &type);
            return type;
        }

        std::size_t GetTrackedItemCount() const {
            return m_trackedItems.m_pBegin != nullptr
                       ? static_cast<std::size_t>(m_trackedItems.m_pEnd -
                                                  m_trackedItems.m_pBegin)
                       : 0;
        }

        C_Item **GetTrackedItemsBegin() { return m_trackedItems.m_pBegin; }
        C_Item *const *GetTrackedItemsBegin() const {
            return m_trackedItems.m_pBegin;
        }

        /**
         * Width of the weapon-slot array: one entry per E_WeaponEquipSlot.
         *
         * Proven by `EquipItemRaw`, which routes an item by its class into
         * one of two collections -- class 4 into the worn vector above, and
         * class 24 into `*(this + 0x98 + 8 * slot)` where the slot comes off
         * the item's own descriptor. A weapon is therefore never in the worn
         * list, which is why anything that wants to know what a body is
         * carrying has to read both.
         */
        static constexpr std::size_t kWeaponSlotCount = 8;

        /** The item equipped to one weapon slot, or null when it is empty. */
        C_Item *GetWeaponSlotItem(std::size_t slot) const {
            return slot < kWeaponSlotCount ? m_weaponSlotItems[slot] : nullptr;
        }

        float GetAggregateItemValue00F8Raw() const {
            return m_aggregateItemValue00F8;
        }

        float GetAggregateItemValue00FCRaw() const {
            return m_aggregateItemValue00FC;
        }

        /**
         * What the body currently has equipped.
         *
         * Walked in place from the begin/end pair the engine keeps at +0x40:
         * C_EquipmentManager::UnequipAllArmor drains exactly this range, one
         * item at a time from the end, which is what identifies it.
         */
        std::size_t GetEquippedCount() const {
            return m_trackedItems.m_pBegin != nullptr ? static_cast<std::size_t>(m_trackedItems.m_pEnd - m_trackedItems.m_pBegin) : 0;
        }

        C_Item *GetEquippedAt(std::size_t index) const {
            return index < GetEquippedCount() ? m_trackedItems.m_pBegin[index] : nullptr;
        }

        /** Whether `item` occupies any weapon slot. */
        bool IsInWeaponSlot(const C_Item *item) const {
            if (item == nullptr) {
                return false;
            }
            for (std::size_t slot = 0; slot < kWeaponSlotCount; ++slot) {
                if (m_weaponSlotItems[slot] == item) {
                    return true;
                }
            }
            return false;
        }

        /**
         * Everything the body carries: the worn list followed by the weapon
         * slots. SDK convenience over the two native collections.
         */
        std::size_t GetCarriedCount() const {
            return GetEquippedCount() + kWeaponSlotCount;
        }

        C_Item *GetCarriedAt(std::size_t index) const {
            const std::size_t wornCount = GetEquippedCount();
            return index < wornCount ? m_trackedItems.m_pBegin[index] : GetWeaponSlotItem(index - wornCount);
        }

      private:
        rpgmodule::C_Soul *m_pSoul;                 // 0x08
        void *m_values0010[6];                      // 0x10, ctor 0x180A30D68 zeroes
        EquipmentItemVectorLayout m_trackedItems;   // 0x40
        std::byte m_hashMap0058[0x40];              // 0x58, native std::unordered_map (built by 0x1806030E4: load factor 1.0, 8 buckets)
        C_Item *m_weaponSlotItems[kWeaponSlotCount]; // 0x98, keyed on E_WeaponEquipSlot
        EquipmentBitVectorLayout m_cachedItemFlags; // 0xD8
        float m_aggregateItemValue00F8;             // 0xF8
        float m_aggregateItemValue00FC;             // 0xFC
        std::byte m_vector0100[0x18];               // 0x100, native std::vector (built by 0x180A30E6C, reserved to 1)
        void *m_value0118;                          // 0x118, zeroed by 0x180A30E6C
        std::uint16_t m_state0120;                  // 0x120, zeroed by 0x180A30E6C
        std::byte m_padding0122[0x6];               // 0x122
        std::uint32_t m_state0128;                  // 0x128
        std::byte m_padding012C[0x4];               // 0x12C

        static void AssertLayout() {
            static_assert(offsetof(C_EquipmentManager, m_pSoul) == 0x08,
                          "C_EquipmentManager::m_pSoul offset mismatch");
            static_assert(offsetof(C_EquipmentManager, m_trackedItems) == 0x40,
                          "C_EquipmentManager::m_trackedItems offset mismatch");
            static_assert(offsetof(C_EquipmentManager, m_weaponSlotItems) == 0x98, "C_EquipmentManager::m_weaponSlotItems offset mismatch");
            static_assert(offsetof(C_EquipmentManager, m_weaponSlotItems) + E_WeaponEquipSlot::Torch * sizeof(C_Item *) == 0xC8, "C_EquipmentManager torch slot offset mismatch");
            static_assert(offsetof(C_EquipmentManager, m_cachedItemFlags) == 0xD8,
                          "C_EquipmentManager::m_cachedItemFlags offset mismatch");
            static_assert(offsetof(C_EquipmentManager, m_hashMap0058) == 0x58, "C_EquipmentManager::m_hashMap0058 offset mismatch");
            static_assert(offsetof(C_EquipmentManager, m_vector0100) == 0x100, "C_EquipmentManager::m_vector0100 offset mismatch");
            static_assert(offsetof(C_EquipmentManager, m_state0120) == 0x120, "C_EquipmentManager::m_state0120 offset mismatch");
            static_assert(offsetof(C_EquipmentManager, m_state0128) == 0x128,
                          "C_EquipmentManager::m_state0128 offset mismatch");
        }
    };

    static_assert(sizeof(C_EquipmentManager) == 0x130, "C_EquipmentManager size mismatch");
    static_assert(sizeof(C_EquipmentManager::E_QuickSlotSpec::Type) == 0x4,
                  "E_QuickSlotSpec::Type size mismatch");
    static_assert(sizeof(C_EquipmentManager::E_EquipPolicy) == 0x4,
                  "E_EquipPolicy size mismatch");
} // namespace SDK::wh::entitymodule
