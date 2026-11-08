#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Item;

    /** Common contract for inventories, stashes, slots, and spawned-item sets. */
    class C_ItemHolder {
      public:
        virtual ~C_ItemHolder()   = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() {}
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() {}
        virtual void unk_fn0005() {}
        virtual void unk_fn0006() {}
        virtual void unk_fn0007() {}
        virtual void unk_fn0008() {}
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() {}
        virtual void unk_fn0012() {}
        virtual void unk_fn0013() {}
        virtual void unk_fn0014() {}
        virtual void unk_fn0015() {}
        virtual void unk_fn0016() {}
        virtual void unk_fn0017() {}
        virtual void unk_fn0018() {}
        virtual void unk_fn0019() {}
        virtual void unk_fn0020() {}
        virtual void unk_fn0021() {}
        virtual void unk_fn0022() {}
        virtual void unk_fn0023() {}

        /**
         * Moves only the physical-holder relationship here; RPG ownership is
         * preserved.
         */
        C_Item *TransferItemToHolder(C_Item *item, bool preventMerge) {
            return hook::this_call<C_Item *>(gPatterns.C_ItemHolder__TransferItemToHolder, this, item, preventMerge);
        }

        // preventMerge keeps the clone in a separate stack.
        C_Item *CloneItem(C_Item *item, std::uint32_t reason, bool preventMerge) {
            return hook::this_call<C_Item *>(gPatterns.C_ItemHolder__CloneItem, this, item, reason, preventMerge);
        }

        /**
         * Removes `count` units through the holder's native bookkeeping.
         *
         * A nonzero count below the current stack applies a signed stack delta
         * and retains the item. Zero or a full count performs complete holder,
         * physical binding, and WUID cleanup.
         */
        void RemoveItem(C_Item *item, std::uint32_t reason = 0x26, std::uint32_t count = 0) {
            hook::this_call<void>(gPatterns.C_ItemHolder__RemoveItem, this, item, reason, count);
        }
    };

    static_assert(sizeof(C_ItemHolder) == 0x8, "C_ItemHolder size mismatch");
} // namespace SDK::wh::entitymodule
