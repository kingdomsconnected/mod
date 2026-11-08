#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Native descriptor shared by item pickup and holder-transfer paths.
     *
     * The constructor at 0x1808CFDEC and copy path at 0x182A67E2C prove the
     * complete 0xC8-byte extent. The native string at +0x18 and callback at
     * +0x70 must be constructed and destroyed by the game allocator.
     */
    class S_ItemTransferDescriptor {
      public:
        std::uint64_t GetItemWuid() const {
            return m_itemWuid;
        }

        /**
         * Where `CompleteItemTransfer` puts the item, or null for the actor's
         * own inventory. `C_ActorActionCarryItem::CompleteItemPickup`
         * (0x180D7C2D4) names the carry action's own holder here, which is
         * how a carried item never reaches the carrier's bags.
         */
        const void *GetDestinationHolder() const {
            return m_pDestinationHolder;
        }

      private:
        std::uint64_t m_itemWuid;                  // 0x00
        std::int32_t m_actionType;                 // 0x08
        bool m_useActionType;                      // 0x0C
        bool m_immediate;                          // 0x0D
        bool m_notifyInventory;                    // 0x0E
        bool m_transferDirectly;                   // 0x0F
        bool m_alternateAnimation;                 // 0x10
        std::byte m_padding0011[0x7];              // 0x11
        SDK::CryStringT<char> m_animationSelector; // 0x18
        void *m_value0020;                         // 0x20, ctor 0x1808CFDEC zeroes
        std::uint32_t m_value0028;                 // 0x28, ctor zeroes
        float m_value002C;                         // 0x2C, ctor sets 1.0
        std::byte m_opaque0030[0x1C];              // 0x30
        std::uint8_t m_flag004C;                   // 0x4C, ctor zeroes
        std::byte m_padding004D[0x3];              // 0x4D
        std::uint32_t m_value0050;                 // 0x50, ctor zeroes
        std::int32_t m_animationOverride;          // 0x54, ctor sets -1
        void *m_values0058[2];                     // 0x58, ctor zeroes
        std::uint32_t m_value0068;                 // 0x68, ctor zeroes
        std::byte m_padding006C[0x4];              // 0x6C
        std::byte m_nativeCallback0070[0x40];      // 0x70
        std::uint32_t m_state00B0;                 // 0xB0
        std::uint32_t m_state00B4;                 // 0xB4
        std::uint32_t m_amount;                    // 0xB8
        std::uint32_t m_value00BC;                 // 0xBC, ctor 0x1808CFDEC zeroes 0xB4..0xBC with one qword store
        void *m_pDestinationHolder;                // 0xC0

        static void AssertLayout() {
            static_assert(offsetof(S_ItemTransferDescriptor, m_itemWuid) == 0x00, "S_ItemTransferDescriptor::m_itemWuid offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_actionType) == 0x08, "S_ItemTransferDescriptor::m_actionType offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_useActionType) == 0x0C, "S_ItemTransferDescriptor::m_useActionType offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_animationSelector) == 0x18, "S_ItemTransferDescriptor::m_animationSelector offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_animationOverride) == 0x54, "S_ItemTransferDescriptor::m_animationOverride offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_value002C) == 0x2C, "S_ItemTransferDescriptor::m_value002C offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_flag004C) == 0x4C, "S_ItemTransferDescriptor::m_flag004C offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_values0058) == 0x58, "S_ItemTransferDescriptor::m_values0058 offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_nativeCallback0070) == 0x70, "S_ItemTransferDescriptor::m_nativeCallback0070 offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_value00BC) == 0xBC, "S_ItemTransferDescriptor::m_value00BC offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_amount) == 0xB8, "S_ItemTransferDescriptor::m_amount offset mismatch");
            static_assert(offsetof(S_ItemTransferDescriptor, m_pDestinationHolder) == 0xC0, "S_ItemTransferDescriptor::m_pDestinationHolder offset mismatch");
        }
    };

    static_assert(sizeof(S_ItemTransferDescriptor) == 0xC8, "S_ItemTransferDescriptor size mismatch");
} // namespace SDK::wh::entitymodule
