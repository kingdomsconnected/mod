#pragma once

#include "../../patterns.h"

#include <cstdint>
#include <vector>

namespace SDK::wh::entitymodule {
    class C_Item;

    /** Native descriptor-match record passed through inventory mutations. */
    struct S_ItemWithAmount {
        C_Item *m_pItem;          // 0x00
        std::uint32_t m_amount;   // 0x08
        std::uint32_t m_padding;  // 0x0C
    };

    static_assert(sizeof(S_ItemWithAmount) == 0x10,
                  "S_ItemWithAmount size mismatch");

    /** Exact ten-slot root contract of native inventory descriptors. */
    class I_ItemDescriptor {
      public:
        virtual ~I_ItemDescriptor() = default;
        virtual std::vector<S_ItemWithAmount>
        unk_fn0001(const std::vector<C_Item *> &inputItems,
                   bool nativeFlag) = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;

        /**
         * NOTE: the engine's matcher overloads return std::vector by value.
         *
         * The vector is allocated with the game's allocator, so one destroyed on
         * this side would free the game's buffer through a different heap. They are
         * deliberately not wrapped; read item lists in place instead, the way
         * C_InventoryBase::GetItemAt does.
             */

        /** Tests the reflected descriptor match against an item snapshot. */
        bool InvokeFullyMatchesItems(
            const std::vector<C_Item *> &inputItems) {
            return hook::this_call<bool>(gPatterns.I_ItemDescriptor__FullyMatchesItems, this,
                                                                inputItems);
        }
    };

    static_assert(sizeof(I_ItemDescriptor) == 0x8,
                  "I_ItemDescriptor size mismatch");
} // namespace SDK::wh::entitymodule
