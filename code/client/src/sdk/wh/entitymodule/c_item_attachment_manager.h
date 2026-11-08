#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>
#include "../framework/wuid.h"
#include "e_weapon_equip_slot.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Human;
    class C_Item;

    /**
     * What a human body is holding, and what it would be holding if it drew.
     *
     * This is the whole of KCD2's weapon system on the actor side. A weapon
     * is not an object of its own: it is an ordinary `C_Item` whose class
     * descriptor names a `weapon_class.xml` row, and this manager is the
     * eight-slot table that says which item belongs in which place on the
     * body. Drawing is not an animation -- it is moving an item from its
     * holster attachment to a hand attachment, which is what `DrawWeapons`
     * does directly and what the draw animation does for itself by firing
     * the `Attach`, `AttachLeft` and `AttachRight` anim events. The
     * animation is the part a player sees; the attachment is the state.
     *
     * Every human owns one. `C_Human::EnsureItemAttachmentManager` allocates
     * exactly 0xE0 bytes from `C_Human::Init` and stores the manager at
     * `C_Human+0x9D0`; it is reached through `C_Actor` vtable slot 191,
     * whose `C_Actor` implementation is the shared `return 0` stub, so an
     * actor that is not a human has none.
     *
     * The table at 0x183EC9958 contains exactly eight entries, the last of
     * which is the scalar deleting destructor -- the destructor was declared
     * last in the original class, so it is `unk_fn0007` here rather than the
     * first slot. The object is owned by its actor and released from
     * `C_Human::Release`; nothing here may destroy one.
     */
    class C_ItemAttachmentManager {
      public:
        /**
         * Which hand `AttachItem` puts the item in.
         *
         * `FromWeaponClass` asks the item's own weapon class, which is what
         * `DrawWeapons` passes for both items. It is resolved to 0 or 1
         * before the attachment work, which refuses anything else -- so a
         * non-weapon given `FromWeaponClass` attaches nothing.
         */
        struct E_AttachHand {
            enum Type : std::uint32_t {
                Right           = 0,
                Left            = 1,
                FromWeaponClass = 2,
            };
        };

        /**
         * The item class id the engine tests a slot against when it asks
         * whether a weapon is drawn.
         *
         * Proven only from `IsWeaponDrawn`'s own use of it, which is why it
         * lives here rather than in an enum of item classes: nothing else in
         * the recovered code names 27, and the rest of the class ids remain
         * unknown.
         */
        static constexpr std::uint32_t kWeaponItemClassId = 27;

        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0; // scalar deleting destructor, +0x38

        /** The body this manager belongs to. */
        C_Human *GetOwner() const { return m_pOwner; }

        /** Which of the three weapon sets the body would draw. */
        E_WeaponSet::Type GetSelectedWeaponSet() const { return static_cast<E_WeaponSet::Type>(m_selectedWeaponSet); }

        /** The item equipped to one slot, or an invalid id when the slot is empty. */
        framework::WUID GetSlotItemId(E_WeaponEquipSlot::Type slot) const { return slot < E_WeaponEquipSlot::Count ? m_slotItems[slot] : framework::WUID {}; }

        /** Whether a slot holds anything. Empty is the invalid marker, not zero. */
        bool IsSlotOccupied(E_WeaponEquipSlot::Type slot) const {
            return slot < E_WeaponEquipSlot::Count && m_slotItems[slot].value != GetInvalidItemId().value;
        }

        /** Which equip slot holds this item, or Count when none does. */
        E_WeaponEquipSlot::Type FindSlotForItem(framework::WUID itemId) const {
            if (itemId.value == GetInvalidItemId().value) {
                return E_WeaponEquipSlot::Count;
            }
            for (std::size_t slot = 0; slot < E_WeaponEquipSlot::Count; ++slot) {
                if (m_slotItems[slot].value == itemId.value) {
                    return static_cast<E_WeaponEquipSlot::Type>(slot);
                }
            }
            return E_WeaponEquipSlot::Count;
        }

        /**
         * Whether a slot is one of the six a weapon set covers.
         *
         * Not the same question as "is it a weapon": `weapon_class.xml` files
         * the torch as a MeleeWeaponClass, so it passes the class test while
         * living outside every set. The engine folds any slot above 5 to set
         * 0, so a torch sent down the set path draws the melee pair instead.
         */
        static constexpr bool IsWeaponSetSlot(E_WeaponEquipSlot::Type slot) {
            return slot < E_WeaponEquipSlot::Torch;
        }

        /** The invalid-item marker the engine fills empty slots with. */
        static framework::WUID GetInvalidItemId() { return *reinterpret_cast<const framework::WUID *>(gPatterns.g_InvalidItemWUID); }

        /**
         * Draws the selected set, with no animation and no action queued.
         *
         * Attaches both hands of the selected set at once, which is what the
         * `DrawWeapon` script binding does and therefore what quests do. A
         * body drawn this way ends up in exactly the state the animated draw
         * leaves it in, which is what makes this the right call for a puppet
         * following someone else's weapon state.
         */
        void InvokeDrawWeapons() { hook::this_call<void>(gPatterns.C_ItemAttachmentManager__DrawWeapons, this); }

        /** Puts the selected set away again, the same way and as directly. */
        void InvokeHolsterWeapons() { hook::this_call<void>(gPatterns.C_ItemAttachmentManager__HolsterWeapons, this); }

        /**
         * Chooses which set the next draw will bring out.
         *
         * Refuses `E_WeaponSet::Any`, and refuses outright while anything is
         * still in the body's hands -- it returns `AreHandsEmpty()` and
         * writes nothing when that is false. So a set change is holster,
         * select, draw, in that order, which is the order the game's own
         * `ToggleWeaponSet` binding uses.
         */
        bool InvokeSetSelectedWeaponSet(E_WeaponSet::Type set) { return hook::this_call<bool>(gPatterns.C_ItemAttachmentManager__SetSelectedWeaponSet, this, static_cast<std::int32_t>(set)); }

        /** Brings out `set` in the order the selection requires: holster, select, draw. */
        void DrawWeaponSet(E_WeaponSet::Type set) {
            InvokeHolsterWeapons();
            InvokeSetSelectedWeaponSet(set);
            InvokeDrawWeapons();
        }

        /** Whether either hand of a set has an item equipped to it at all. */
        bool InvokeHasWeaponInSet(E_WeaponSet::Type set) const { return hook::this_call<bool>(gPatterns.C_ItemAttachmentManager__HasWeaponInSet, this, static_cast<std::int32_t>(set)); }

        /** Whether the body is holding nothing in either hand. */
        bool InvokeAreHandsEmpty() const { return hook::this_call<bool>(gPatterns.C_ItemAttachmentManager__AreHandsEmpty, this); }

        /**
         * Whether a weapon of one item class is currently in hand.
         *
         * `set` may be `E_WeaponSet::Any`, which walks the body's two live
         * hand items instead of one set's slots. `includeHeld` widens the
         * test past what is attached to what the body is merely handling,
         * which is what the `IsWeaponDrawn` script binding passes.
         */
        bool InvokeIsWeaponDrawn(bool includeHeld = true, E_WeaponSet::Type set = E_WeaponSet::Any, std::uint32_t itemClassId = kWeaponItemClassId) const {
            return hook::this_call<bool>(gPatterns.C_ItemAttachmentManager__IsWeaponDrawn, this, includeHeld, static_cast<std::int32_t>(set), itemClassId);
        }

        /**
         * Attaches one item by id, resolving it through the world's item
         * registry first.
         *
         * The lower-level half of a draw, and the one the anim events reach.
         * Prefer `InvokeDrawWeapons` for a whole set: this does not touch the
         * selected-set index, so an item attached on its own leaves the
         * manager describing a body that never drew.
         */
        bool InvokeAttachItemById(framework::WUID itemId, E_AttachHand::Type hand = E_AttachHand::FromWeaponClass) { return hook::this_call<bool>(gPatterns.C_ItemAttachmentManager__AttachItemById, this, itemId, static_cast<std::uint32_t>(hand)); }

        /** Takes one item back off the body, by id, and its matching half. */
        bool InvokeDetachItemById(framework::WUID itemId) { return hook::this_call<bool>(gPatterns.C_ItemAttachmentManager__DetachItemById, this, itemId); }

        framework::WUID FindMissileDefenseItem() const {
            framework::WUID result {};

            hook::this_call<void>(gPatterns.C_ItemAttachmentManager__FindMissileDefenseItem, this, &result);

            return result;
        }

        /**
         * Asks the owner's movement state machine to drive an item-handling
         * transition (state event 118; the torch uses mode 3).
         *
         * True only when a state answered by installing the transition driver
         * at +0xD8 -- the driver that spawns the held item's entity, attaches
         * it to the hand and manages the torch fire. Every native held-item
         * visual is behind this answer, so a body whose machine ignores the
         * event can be given hand records all day and never show a thing.
         */
        bool InvokeRequestItemHandling(std::uint8_t mode) { return hook::this_call<bool>(gPatterns.C_ItemAttachmentManager__RequestItemHandling, this, mode); }

        /**
         * Completes the driver `InvokeRequestItemHandling` installed.
         *
         * Calls slot 1 of the object at +0xD8 with this manager's owner, and
         * does nothing at all when no driver is installed.
         */
        void InvokeFinishItemHandling() { hook::this_call<void>(gPatterns.C_ItemAttachmentManager__FinishItemHandling, this); }

        /**
         * The item the body is handling right now, or the invalid marker.
         *
         * Takes no hand: `GetHandledItemId` reads one id off the actor's own
         * item state (`C_Actor+0x990`, sub-object +0x258, table slot 1) and
         * the body handles one item at a time. A hand passed here would be
         * ignored, so none is accepted.
         */
        framework::WUID InvokeGetHandledItemId() const {
            framework::WUID result {};
            hook::this_call<void>(gPatterns.C_ItemAttachmentManager__GetHandledItemId, this, &result);
            return result;
        }

        /**
         * Runs the same native torch put-away route as the local R-toggle.
         *
         * Reads the Torch slot itself (+0x78) and does nothing when it is
         * empty, then delegates to the same detach `InvokeDetachItemById`
         * reaches -- so this is the torch-shaped shortcut, not a second path.
         */
        bool InvokeHolsterTorch() { return hook::this_call<bool>(gPatterns.C_ItemAttachmentManager__HolsterTorch, this); }

      private:
        std::byte m_opaque0008[0x10];  // 0x08
        std::byte m_opaque0018[0x10];  // 0x18, listener list; C_Human::Release erases from it
        std::byte m_opaque0028[0x10];  // 0x28, the sink AttachItem notifies
        C_Human *m_pOwner;             // 0x38
        std::int32_t m_selectedWeaponSet; // 0x40
        std::byte m_padding0044[0x4];
        framework::WUID m_slotItems[E_WeaponEquipSlot::Count]; // 0x48
        std::uint32_t m_slotStates[E_WeaponEquipSlot::Count];  // 0x88
        std::uint16_t m_state00A8;     // 0xA8
        std::byte m_padding00AA[0x6];
        std::byte m_vector00B0[0x18];  // 0xB0, a native std::vector; ctor 0x1808D2A04 zeroes it
        void *m_value00C8;             // 0xC8, ctor zeroes
        std::uint16_t m_state00D0;     // 0xD0
        std::byte m_padding00D2[0x6];
        void *m_pItemHandlingDriver;   // 0xD8, installed by RequestItemHandling

        static void AssertLayout() {
            static_assert(offsetof(C_ItemAttachmentManager, m_pOwner) == 0x38, "C_ItemAttachmentManager::m_pOwner offset mismatch");
            static_assert(offsetof(C_ItemAttachmentManager, m_selectedWeaponSet) == 0x40, "C_ItemAttachmentManager::m_selectedWeaponSet offset mismatch");
            static_assert(offsetof(C_ItemAttachmentManager, m_slotItems) == 0x48, "C_ItemAttachmentManager::m_slotItems offset mismatch");
            static_assert(offsetof(C_ItemAttachmentManager, m_slotStates) == 0x88, "C_ItemAttachmentManager::m_slotStates offset mismatch");
            static_assert(offsetof(C_ItemAttachmentManager, m_state00A8) == 0xA8, "C_ItemAttachmentManager::m_state00A8 offset mismatch");
            static_assert(offsetof(C_ItemAttachmentManager, m_state00D0) == 0xD0, "C_ItemAttachmentManager::m_state00D0 offset mismatch");
            static_assert(offsetof(C_ItemAttachmentManager, m_vector00B0) == 0xB0, "C_ItemAttachmentManager::m_vector00B0 offset mismatch");
            static_assert(offsetof(C_ItemAttachmentManager, m_value00C8) == 0xC8, "C_ItemAttachmentManager::m_value00C8 offset mismatch");
            static_assert(offsetof(C_ItemAttachmentManager, m_pItemHandlingDriver) == 0xD8, "C_ItemAttachmentManager::m_pItemHandlingDriver offset mismatch");
        }
    };

    static_assert(sizeof(C_ItemAttachmentManager) == 0xE0, "C_ItemAttachmentManager size mismatch");
} // namespace SDK::wh::entitymodule
