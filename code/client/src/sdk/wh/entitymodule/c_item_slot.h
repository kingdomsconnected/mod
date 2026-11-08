#pragma once

#include "../framework/i_schedule_updatable.h"
#include "c_item_holder.h"
#include "c_light_common_editor_properties.h"
#include "i_game_object_light.h"
#include "s_item_class_guid.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Item;

    /**
     * Spawnable world-item slot shared by piles, books, and multiclass slots.
     *
     * The complete-object constructor at 0x1803A6744 and RTTI establish the
     * four visible bases at +0x00, +0x08, +0x38, and +0x68. Native RTTI also
     * reports an empty UnsafeOp::CryDeferrable<0> tag at +0x70; it overlaps
     * the first data member through empty-base optimization and is omitted
     * here so this overlay remains compiler-independent.
     */
    class C_ItemSlot : public C_ItemHolder,
                       public I_GameObjectLight,
                       public C_LightCommonEditorProperties,
                       public framework::I_ScheduleUpdatable {
      public:
        ~C_ItemSlot() override = default;

        // C_ItemSlot extends C_ItemHolder's 24-entry primary table with
        // fourteen contracts. Their source names are not yet proven.
        virtual void unk_item_slot_fn0024() = 0;
        virtual void unk_item_slot_fn0025() = 0;
        virtual void unk_item_slot_fn0026() = 0;
        virtual void unk_item_slot_fn0027() = 0;
        virtual void unk_item_slot_fn0028() = 0;
        virtual void unk_item_slot_fn0029() = 0;
        virtual void unk_item_slot_fn0030() = 0;
        virtual void unk_item_slot_fn0031() = 0;
        virtual void unk_item_slot_fn0032() = 0;
        virtual void unk_item_slot_fn0033() = 0;
        virtual void unk_item_slot_fn0034() = 0;
        virtual void unk_item_slot_fn0035() = 0;
        virtual void unk_item_slot_fn0036() = 0;
        virtual void unk_item_slot_fn0037() = 0;

        /** The slot behind an ItemSlot entity's `EEntityProxy::GameObjectLight` proxy, its `I_GameObjectLight` base. */
        static C_ItemSlot *FromProxy(IEntityProxy *proxy) {
            return static_cast<C_ItemSlot *>(static_cast<I_GameObjectLight *>(proxy));
        }

        const S_ItemClassGUID &GetItemClassGuid() const {
            return m_itemClassGuid;
        }

        /** Reflected as `GetMultiSlotItem`; returns the item at +0x98. */
        C_Item *GetMultiSlotItem() const { return m_pCurrentItem; }

        std::int32_t GetViewDistanceRatio() const {
            return m_viewDistanceRatio;
        }

        /** `nRestockPeriodDays` -- days before a taken item comes back, 0 to disable. */
        std::uint16_t GetRestockPeriodDays() const { return m_restockPeriodDays; }

        /** `nDestockPeriodDays` -- days before a returned item goes away again. */
        std::uint16_t GetDestockPeriodDays() const { return m_destockPeriodDays; }

        bool SpawnsOnStart() const { return (m_placementFlags & kPlacementSpawnOnStart) != 0; }
        bool HasInvisibleContent() const { return (m_placementFlags & kPlacementInvisibleContent) != 0; }
        bool DynamicSpawningDisabled() const { return (m_spawnFlags & kSpawnDisableDynamic) != 0; }

        /**
         * Puts the slot in the dormant state the level itself can author --
         * both bits are authored properties, and thousands of shipped slots
         * already carry `bSpawnOnStart="0"`, so this is a configuration the
         * engine produces rather than one forced on it.
         *
         * Only meaningful before the slot has populated, so the one place to
         * call it is immediately after the native property load has run.
         */
        void SuppressSpawning() {
            m_placementFlags &= static_cast<std::uint8_t>(~kPlacementSpawnOnStart);
            m_spawnFlags |= kSpawnDisableDynamic;
        }

      private:
        // `C_ItemSlot::LoadScriptProperties` (0x181EEE490) and its sibling
        // overload (0x181EEE7D0) are the only two writers of these bits.
        static constexpr std::uint8_t kPlacementSpawnOnStart              = 1U << 0;
        static constexpr std::uint8_t kPlacementInteractiveCollisionClass = 1U << 1;
        static constexpr std::uint8_t kPlacementOnlyNPC                   = 1U << 2;
        static constexpr std::uint8_t kPlacementInvisibleContent          = 1U << 3;

        static constexpr std::uint8_t kSpawnHasItemPhysics   = 1U << 1;
        static constexpr std::uint8_t kSpawnDisableDynamic   = 1U << 2;
        static constexpr std::uint8_t kSpawnSaveBorrowedItem = 1U << 3;

        S_ItemClassGUID m_itemClassGuid;    // 0x70, `guidItemClassId`
        const char *m_pItemClassFilter;     // 0x80, `icfItemClassFilter`, CryStringT<char> payload
        const char *m_pManipulationAnimTag; // 0x88, `sManipulationAnimTag`, CryStringT<char> payload
        void *m_pState0090;
        C_Item *m_pCurrentItem;             // 0x98, reflected GetMultiSlotItem result
        std::byte m_state00A0[0x10];
        void *m_pState00B0;
        std::int32_t m_state00B8;           // initialized to -1
        std::uint16_t m_restockPeriodDays;  // 0xBC, `nRestockPeriodDays`
        std::uint16_t m_destockPeriodDays;  // 0xBE, `nDestockPeriodDays`
        std::uint8_t m_placementFlags;      // 0xC0, kPlacement* above
        std::uint8_t m_spawnFlags;          // 0xC1, kSpawn* above
        std::uint16_t m_state00C2;
        float m_quality;                    // 0xC4, `nQuality`
        float m_itemHealth;                 // 0xC8, `ItemHealth`, -1 for the class default
        float m_condition;                  // 0xCC, `Condition`, -1 for the class default
        float m_blood;                      // 0xD0, `Blood`, clamped to 0..1 on load
        std::int32_t m_viewDistanceRatio;   // 0xD4, XML `ViewDistRatio`
        std::uint64_t m_state00D8;          // initialized to UINT64_MAX

        static void AssertLayout() {
            static_assert(offsetof(C_ItemSlot, m_itemClassGuid) == 0x70, "C_ItemSlot::m_itemClassGuid offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_pItemClassFilter) == 0x80, "C_ItemSlot::m_pItemClassFilter offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_pManipulationAnimTag) == 0x88, "C_ItemSlot::m_pManipulationAnimTag offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_pCurrentItem) == 0x98, "C_ItemSlot::m_pCurrentItem offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_restockPeriodDays) == 0xBC, "C_ItemSlot::m_restockPeriodDays offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_destockPeriodDays) == 0xBE, "C_ItemSlot::m_destockPeriodDays offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_placementFlags) == 0xC0, "C_ItemSlot::m_placementFlags offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_spawnFlags) == 0xC1, "C_ItemSlot::m_spawnFlags offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_quality) == 0xC4, "C_ItemSlot::m_quality offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_itemHealth) == 0xC8, "C_ItemSlot::m_itemHealth offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_condition) == 0xCC, "C_ItemSlot::m_condition offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_blood) == 0xD0, "C_ItemSlot::m_blood offset mismatch");
            static_assert(offsetof(C_ItemSlot, m_viewDistanceRatio) == 0xD4, "C_ItemSlot::m_viewDistanceRatio offset mismatch");
        }
    };

    static_assert(sizeof(C_ItemSlot) == 0xE0, "C_ItemSlot size mismatch");
} // namespace SDK::wh::entitymodule
