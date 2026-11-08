#pragma once

#include "../../patterns.h"

#include "../framework/wuid.h"
#include "c_inventory_base.h"
#include "c_item_collection.h"
#include "c_item_reference_descriptor.h"
#include "c_item_reference_vector_descriptor.h"
#include "s_item_class_guid.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace SDK::wh::entitymodule {
    class C_Item;
    class C_ItemClassDescriptor;

    /** Concrete inventory returned by the RPG soul inventory subsystem. */
    class C_Inventory
        : public C_InventoryBase
        , public C_ItemCollection {
      public:
        // Native Inventory.DeleteItemOfClass and RemoveMoney script bridges.
        static constexpr std::uint32_t kScriptRemovalReason = 2;

        ~C_Inventory() override = default;

        void unk_fn0000() override;
        void unk_fn0001() override;
        void unk_fn0002() override;
        void unk_fn0003() override;
        void unk_fn0004() override;
        void unk_fn0005() override;
        void unk_fn0006() override;
        void unk_fn0007() override;
        void unk_fn0008() override;
        void unk_fn0009() override;
        void unk_fn0010() override;
        void unk_fn0011() override;
        void unk_fn0012() override;
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

        /** Count zero transfers the item's complete current stack. */
        C_Item *InvokeAddItemRaw(C_Item *item, std::uint32_t count, bool nativeFlag) {
            return hook::this_call<C_Item *>(gPatterns.C_Inventory__AddItemRaw, this, item, count, nativeFlag);
        }

        void InvokeRemoveAllItemsRaw() {
            hook::this_call<void>(gPatterns.C_Inventory__RemoveAllItemsRaw, this);
        }

        C_Item *InvokeFindItemByClassRaw(const S_ItemClassGUID &classId) {
            return hook::this_call<C_Item *>(gPatterns.C_Inventory__FindItemByClassRaw, this, &classId);
        }

        /** Returns the reflected `Items` snapshot by value. */
        /**
         * The items this inventory holds, copied into the caller's own heap.
         *
         * Built from the base's begin/end pair rather than through the engine's
         * own GetItems, which copy-constructs a std::vector using the game's
         * allocator -- destroying that on this side frees the game's buffer
         * through a different heap and corrupts it. The pair read here is the
         * very vector that overload copies from, so nothing is lost by not
         * calling it.
         */
        std::vector<C_Item *> GetItems() const {
            std::vector<C_Item *> items;
            const std::size_t count = GetItemCount();
            items.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                items.push_back(GetItemAt(i));
            }
            return items;
        }

        /** Returns the reflected total amount of money in this inventory. */
        std::uint32_t GetMoney() const {
            return hook::this_call<std::uint32_t>(gPatterns.C_Inventory__GetMoneyRaw, this);
        }

        /**
         * Removes native money units using the inventory's money-item path.
         *
         * The scripting `RemoveMoney` binding proves reason value 2. The
         * native return is the amount actually removed, allowing callers to
         * detect insufficient funds without bypassing inventory bookkeeping.
         */
        std::uint32_t InvokeRemoveMoneyRaw(std::uint32_t amount, std::uint32_t reason = kScriptRemovalReason) {
            return hook::this_call<std::uint32_t>(gPatterns.C_Inventory__RemoveMoneyRaw, this, &amount, reason);
        }

        /**
         * Invokes the reflected native `CreateItems` member.
         *
         * RTTR proves the argument names and defaults as ItemClass, Amount,
         * ShowUINotification, Quality, Health, and Condition.
         */
        std::shared_ptr<C_ItemClassDescriptor> CreateItems(const S_ItemClassGUID &itemClass, int amount = 1, bool showUINotification = true, std::uint32_t quality = 0, float health = -1.0F, float condition = -1.0F) {
            std::shared_ptr<C_ItemClassDescriptor> result;
            hook::this_call<void>(gPatterns.C_Inventory__CreateItems, this, &result, &itemClass, amount, showUINotification, quality, health, condition);
            return result;
        }

        /**
         * Removes up to `amount` units of one class and returns how many went.
         *
         * The inverse of `CreateItems`, and the whole of the game's own
         * `Inventory.DeleteItemOfClass` binding, which is what proves reason 2.
         * Counts units rather than stacks, and matches on `GetOwner() == this`,
         * so an item still counts while it sits in a hand. An `amount` of 0
         * means every one of that class, not none.
         */
        std::uint32_t RemoveItemsByClass(const S_ItemClassGUID &itemClass, int amount, std::uint32_t reason = kScriptRemovalReason) {
            return hook::this_call<std::uint32_t>(gPatterns.C_Inventory__RemoveItemsByClass, this, &itemClass, amount, reason);
        }

        /** Invokes the reflected native `DeleteItems` member. */
        bool DeleteItems(std::shared_ptr<I_ItemDescriptor> itemDescriptor, bool showUINotification, bool allowPartialMatch) {
            return hook::this_call<bool>(gPatterns.C_Inventory__DeleteItems, this, std::move(itemDescriptor), showUINotification, allowPartialMatch);
        }

        /** Invokes the reflected native `MoveItems` member on this source. */
        bool MoveItems(std::shared_ptr<I_ItemDescriptor> itemDescriptor, C_Inventory *targetInventory, bool allowPartialMatch, bool changeOwner, bool showUINotification = true) {
            return hook::this_call<bool>(gPatterns.C_Inventory__MoveItems, this, std::move(itemDescriptor), targetInventory, allowPartialMatch, changeOwner, showUINotification);
        }

        /** Deletes one concrete item through the native descriptor path. */
        bool DeleteItem(C_Item *item, bool showUINotification = true, bool allowPartialMatch = false) {
            return item != nullptr && DeleteItems(CreateItemReferenceDescriptor(item), showUINotification, allowPartialMatch);
        }

        /** Deletes concrete item stacks through one native descriptor call. */
        bool DeleteItems(const std::vector<C_Item *> &items, bool showUINotification = true, bool allowPartialMatch = false) {
            return !items.empty() && DeleteItems(CreateItemReferenceVectorDescriptor(items), showUINotification, allowPartialMatch);
        }

        /** Moves one concrete item through the native descriptor path. */
        bool MoveItem(C_Item *item, C_Inventory *targetInventory, bool allowPartialMatch = false, bool changeOwner = true, bool showUINotification = true) {
            return item != nullptr && targetInventory != nullptr && MoveItems(CreateItemReferenceDescriptor(item), targetInventory, allowPartialMatch, changeOwner, showUINotification);
        }

        /** Moves concrete item stacks through one native descriptor call. */
        bool MoveItems(const std::vector<C_Item *> &items, C_Inventory *targetInventory, bool allowPartialMatch = false, bool changeOwner = true, bool showUINotification = true) {
            return !items.empty() && targetInventory != nullptr && MoveItems(CreateItemReferenceVectorDescriptor(items), targetInventory, allowPartialMatch, changeOwner, showUINotification);
        }

        std::vector<C_Item *> InvokeGetItemsRaw() const {
            return GetItems();
        }
        std::uint32_t InvokeGetMoneyRaw() const {
            return GetMoney();
        }

        std::shared_ptr<C_ItemClassDescriptor> InvokeCreateItemsRaw(const S_ItemClassGUID &itemClass, int amount = 1, bool showUINotification = true, std::uint32_t quality = 0, float health = -1.0F, float condition = -1.0F) {
            return CreateItems(itemClass, amount, showUINotification, quality, health, condition);
        }

        bool InvokeDeleteItemsRaw(std::shared_ptr<I_ItemDescriptor> itemDescriptor, bool showUINotification, bool allowPartialMatch) {
            return DeleteItems(std::move(itemDescriptor), showUINotification, allowPartialMatch);
        }

        bool InvokeMoveItemsRaw(std::shared_ptr<I_ItemDescriptor> itemDescriptor, C_Inventory *targetInventory, bool allowPartialMatch, bool changeOwner, bool showUINotification = true) {
            return MoveItems(std::move(itemDescriptor), targetInventory, allowPartialMatch, changeOwner, showUINotification);
        }

        /** Native inventory name exposed by the reflected `Name` getter. */
        const char *GetName() const {
            return m_pName;
        }

        /** Secondary collection slot 0 returns this WUID at +0x158. */
        framework::WUID GetWuid() const {
            return m_wuid;
        }

        const std::byte *GetState0160Raw() const {
            return m_state0160;
        }
        std::uint16_t GetState0178Raw() const {
            return m_state0178;
        }
        bool GetState017ARaw() const {
            return m_state017A;
        }
        void *GetState0180Raw() const {
            return m_pState0180;
        }

      private:
        framework::WUID m_wuid;       // 0x158
        std::byte m_state0160[0x10];  // 0x160
        const char *m_pName;          // 0x170
        std::uint16_t m_state0178;    // 0x178
        bool m_state017A;             // 0x17A
        std::byte m_padding017B[0x5]; // 0x17B
        void *m_pState0180;           // 0x180

        static void AssertLayout() {
            static_assert(offsetof(C_Inventory, m_wuid) == 0x158, "C_Inventory::m_wuid offset mismatch");
            static_assert(offsetof(C_Inventory, m_state0160) == 0x160, "C_Inventory::m_state0160 offset mismatch");
            static_assert(offsetof(C_Inventory, m_pName) == 0x170, "C_Inventory::m_pName offset mismatch");
            static_assert(offsetof(C_Inventory, m_state0178) == 0x178, "C_Inventory::m_state0178 offset mismatch");
            static_assert(offsetof(C_Inventory, m_state017A) == 0x17A, "C_Inventory::m_state017A offset mismatch");
            static_assert(offsetof(C_Inventory, m_pState0180) == 0x180, "C_Inventory::m_pState0180 offset mismatch");
        }
    };

    static_assert(sizeof(C_Inventory) == 0x188, "C_Inventory size mismatch");
} // namespace SDK::wh::entitymodule
