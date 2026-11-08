#pragma once

#include "../../patterns.h"

#include "../../cryengine/entitysystem/entity_event_listener_with_cleanup.h"
#include "../../types.h"
#include "../framework/i_bindable_object.h"
#include "c_armor_runtime_data.h"
#include "c_item_holder.h"
#include "s_item_class.h"
#include "s_item_class_guid.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace SDK::wh::entitymodule {
    enum class E_ItemCategory : std::int32_t;
    class C_PickableItem;
    class I_ItemRuntimeData;

    /** Native RPG inventory item, allocation-proven to occupy 0xA8 bytes. */
    class C_Item
        : public framework::I_BindableObject
        , public SDK::EntityEventListenerWithCleanup {
      public:
        ~C_Item() override = default;
        void unk_bindable_fn0001() override;
        void unk_bindable_fn0002() override;
        void unk_bindable_fn0003() override;
        void OnEntityEvent(SDK::IEntity *entity, SDK::SEntityEvent *event) override;

        std::uint64_t GetId() const {
            return m_id;
        }

        // Reads existing runtime data without allocating.
        I_ItemRuntimeData *GetRuntimeData() const {
            return m_pRuntimeData;
        }

        I_ItemRuntimeData *EnsureRuntimeData() {
            return hook::this_call<I_ItemRuntimeData *>(gPatterns.C_Item__EnsureRuntimeData, this);
        }

        /**
         * The item's dirt and blood, made if it has none yet; null when the
         * item is not armour (`E_ItemClassId::Armor`).
         */
        C_ArmorRuntimeData *GetOrCreateArmorRuntimeData() {
            return hook::this_call<C_ArmorRuntimeData *>(gPatterns.C_Item__GetOrCreateArmorRuntimeData, this);
        }

        void SetQuality(std::uint32_t quality) {
            hook::this_call<void>(gPatterns.C_Item__SetQuality, this, quality);
        }

        std::uint32_t GetAmount() const {
            return m_stackCount;
        }
        std::uint32_t GetStackCount() const {
            return m_stackCount;
        }

        // Absolute health, before condition normalization.
        float GetHealth() const {
            return m_health;
        }

        const S_ItemClass *GetItemClass() const {
            return m_pItemClass;
        }

        // Publishes the same observer event as a quality/health change.
        // RDX is unused; mask 0 leaves class-specific flag effects alone.
        void NotifyChanged() {
            hook::this_call<void>(gPatterns.C_Item__NotifyChanged, this, nullptr, 0U);
        }

        /** Applies a signed stack delta through the owning holder's bookkeeping. */
        bool AdjustAmount(std::int32_t delta) {
            return hook::this_call<bool>(gPatterns.C_Item__AdjustAmount, this, delta);
        }

        /**
         * Raw signatures for natives that return a value too large for a
         * register.
         *
         * MSVC x64 returns anything that is not a 1, 2, 4 or 8 byte trivial
         * type through a hidden pointer, and on a member function that pointer
         * is the SECOND argument -- `this` keeps RCX. Spelling the pointer out
         * is what keeps the two in the right registers: a typedef that returns
         * the value by value from a plain function pointer puts the return
         * slot in RCX instead, and the native then reads the caller's stack
         * slot as its `this`.
         */

        /** Native getter backing the reflected read-only `ItemClass` property. */
        S_ItemClassGUID InvokeGetItemClassRaw() const {
            S_ItemClassGUID result {};
            hook::this_call<void>(gPatterns.C_Item__GetItemClassRaw, this, &result);
            return result;
        }

        /**
         * The item's display name, owned by the game.
         *
         * Deliberately not the std::string overload the engine also exposes.
         * That one allocates the buffer with the game's allocator for any name
         * past the 15-character small-string limit, and a std::string destroyed
         * on this side would hand that buffer to a different heap. Nothing that
         * allocates may cross the module boundary; see GetItemCount/GetItemAt
         * on C_InventoryBase for the same reason.
         */
        const char *InvokeGetNameRaw() const {
            const char *name = hook::this_call<const char *>(gPatterns.C_Item__GetNameCStrRaw, this);
            return name != nullptr ? name : "";
        }

        E_ItemCategory InvokeGetCategoryRaw() const {
            return hook::this_call<E_ItemCategory>(gPatterns.C_Item__GetCategoryRaw, this);
        }

        std::int32_t InvokeGetTypeRaw() const {
            return hook::this_call<std::int32_t>(gPatterns.C_Item__GetTypeRaw, this);
        }

        std::int32_t InvokeGetSubtypeRaw() const {
            return hook::this_call<std::int32_t>(gPatterns.C_Item__GetSubtypeRaw, this);
        }

        std::uint32_t InvokeGetNewUnitPriceRaw() const {
            return hook::this_call<std::uint32_t>(gPatterns.C_Item__GetNewUnitPriceRaw, this);
        }

        std::uint32_t InvokeGetCurrentUnitPriceRaw() const {
            return hook::this_call<std::uint32_t>(gPatterns.C_Item__GetCurrentUnitPriceRaw, this);
        }

        std::uint32_t InvokeGetNewStackPriceRaw() const {
            return hook::this_call<std::uint32_t>(gPatterns.C_Item__GetNewStackPriceRaw, this);
        }

        std::uint32_t InvokeGetCurrentStackPriceRaw() const {
            return hook::this_call<std::uint32_t>(gPatterns.C_Item__GetCurrentStackPriceRaw, this);
        }

        std::uint32_t InvokeGetQualityRaw() const {
            return hook::this_call<std::uint32_t>(gPatterns.C_Item__GetQualityRaw, this);
        }

        float InvokeGetConditionRaw() const {
            return hook::this_call<float>(gPatterns.C_Item__GetConditionRaw, this);
        }

        std::uint32_t InvokeGetMaxQualityRaw() const {
            return hook::this_call<std::uint32_t>(gPatterns.C_Item__GetMaxQualityRaw, this);
        }

        /** Sets the reflected discrete item phase identifier. */
        void InvokeSetItemPhaseIdRaw(std::int32_t phaseId) {
            hook::this_call<void>(gPatterns.C_Item__SetItemPhaseIdRaw, this, phaseId);
        }

        /** Advances the reflected normalized item phase, clamped natively. */
        void InvokeAdvanceItemPhaseRaw(float amount) {
            hook::this_call<void>(gPatterns.C_Item__AdvanceItemPhaseRaw, this, amount);
        }

        // Native clamping uses the item's quality range.
        void InvokeSetItemHealthRaw(float health) {
            hook::this_call<void>(gPatterns.C_Item__SetItemHealthRaw, this, health);
        }

        /** Removes blood and at most `maxEffect` dirt from this item. */
        void InvokeWashItemRaw(float maxEffect = 1.0F) {
            hook::this_call<void>(gPatterns.C_Item__WashItemRaw, this, maxEffect);
        }

        /** Physical CryAction item bound to this RPG item, once spawning finishes. */
        C_PickableItem *GetPickableItem() const {
            return m_pPickableItem;
        }

        /**
         * Creates/binds the native pickable item at an explicit world pose.
         * The callback is invoked immediately when already bound and otherwise
         * retained by the game's spawn queue until binding completes.
         */
        void SpawnPickable(const std::function<void(C_PickableItem &)> &onBound, const SDK::Vec3 &position, const SDK::Quat &rotation) {
            hook::this_call<void>(gPatterns.C_Item__SpawnPickable, this, &onBound, &position, &rotation);
        }

        /** Removes or cancels the physical binding while preserving this RPG item. */
        void UnbindPickable() {
            hook::this_call<void>(gPatterns.C_Item__UnbindPickable, this);
        }

        /** Inventory/item-holder that owns this item. */
        C_ItemHolder *GetOwner() const {
            return m_pOwner;
        }

        /** Current physical holder; null when the owner is the active holder. */
        C_ItemHolder *GetHolder() const {
            return m_pHolder;
        }

        /** Mirrors the native holder lookup: physical holder, then owner. */
        C_ItemHolder *GetEffectiveHolder() const {
            return m_pHolder != nullptr ? m_pHolder : m_pOwner;
        }

        /**
         * Whether the item is equipped on its owner's body.
         *
         * Bit 0 of the flag word. `C_ItemAttachmentManager::IsWeaponDrawn`
         * (0x1808C69B4) requires it of a drawn weapon, the weapon-condition
         * walk at 0x181F9A570 counts only items carrying it, and the
         * pickpocketing loot filter (`GetPickpocketLootGroup`, 0x1809D14FC)
         * leaves every item carrying it out of the grid.
         */
        bool IsEquipped() const {
            return (m_flags & kEquippedFlag) != 0;
        }

        // Factory 0x180466018 stores its R8D reason at 0x1804660C0.
        std::uint32_t GetCreationReason() const {
            return m_creationReason;
        }

      private:
        static constexpr std::uint32_t kEquippedFlag = 1U << 0;

        std::byte m_state0010[0x20];       // 0x10
        std::uint64_t m_id;                // 0x30
        std::byte m_state0038[0x10];       // 0x38
        S_ItemClass *m_pItemClass;         // 0x48, getter 0x1804695B4
        std::uint32_t m_stackCount;        // 0x50
        float m_health;                    // 0x54
        I_ItemRuntimeData *m_pRuntimeData; // 0x58
        std::uint32_t m_flags;             // 0x60, bit 0 equipped; bit 1 read by C_StealItemsEffect::Apply to pick the holder path
        std::uint32_t m_value0064;       // 0x64, ctor 0x181EEDF50 zeroes it with m_flags (one qword store)
        std::uint64_t m_itemId0068;      // 0x68, ctor seeds the invalid WUID (qword_1853257E0, -1)
        std::uint64_t m_itemId0070;      // 0x70, same
        std::uint64_t m_value0078;       // 0x78, ctor zeroes
        std::int64_t m_value0080;        // 0x80, ctor sets INT64_MAX

        C_PickableItem *m_pPickableItem; // 0x88
        C_ItemHolder *m_pOwner;          // 0x90
        C_ItemHolder *m_pHolder;         // 0x98
        std::uint32_t m_state00A0;       // 0xA0
        std::uint32_t m_creationReason;  // 0xA4

        static void AssertLayout() {
            static_assert(offsetof(C_Item, m_id) == 0x30, "C_Item::m_id offset mismatch");
            static_assert(offsetof(C_Item, m_pItemClass) == 0x48, "C_Item::m_pItemClass offset mismatch");
            static_assert(offsetof(C_Item, m_stackCount) == 0x50, "C_Item::m_stackCount offset mismatch");
            static_assert(offsetof(C_Item, m_health) == 0x54, "C_Item::m_health offset mismatch");
            static_assert(offsetof(C_Item, m_pRuntimeData) == 0x58, "C_Item::m_pRuntimeData offset mismatch");
            static_assert(offsetof(C_Item, m_flags) == 0x60, "C_Item::m_flags offset mismatch");
            static_assert(offsetof(C_Item, m_itemId0068) == 0x68, "C_Item::m_itemId0068 offset mismatch");
            static_assert(offsetof(C_Item, m_value0080) == 0x80, "C_Item::m_value0080 offset mismatch");
            static_assert(offsetof(C_Item, m_pPickableItem) == 0x88, "C_Item::m_pPickableItem offset mismatch");
            static_assert(offsetof(C_Item, m_pOwner) == 0x90, "C_Item::m_pOwner offset mismatch");
            static_assert(offsetof(C_Item, m_pHolder) == 0x98, "C_Item::m_pHolder offset mismatch");
            static_assert(offsetof(C_Item, m_creationReason) == 0xA4, "C_Item::m_creationReason offset mismatch");
        }
    };

    static_assert(sizeof(C_Item) == 0xA8, "C_Item size mismatch");
} // namespace SDK::wh::entitymodule
