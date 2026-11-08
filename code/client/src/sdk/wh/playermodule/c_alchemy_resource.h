#pragma once

#include "../entitymodule/c_item_wrapper.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * RTTI 0x183A5CE78 has no base. Both native allocation paths request
     * 0x28 bytes. Constructor 0x1808D4260 moves one inventory unit into
     * C_ItemWrapper; this object owns that unit until return or destruction.
     * Never construct, copy, delete or retain this game-owned view in mod code.
     */
    class C_AlchemyResource {
      public:
        virtual ~C_AlchemyResource() = default;

        SDK::wh::entitymodule::C_ItemWrapper *GetItemWrapper() const {
            return m_itemWrapper;
        }
        SDK::wh::entitymodule::C_Item *GetItem() const {
            return m_itemWrapper != nullptr ? m_itemWrapper->GetItem() : nullptr;
        }
        bool IsMilled() const {
            return (m_flags & 1U) != 0;
        }
        bool IsDistilled() const {
            return (m_flags & 2U) != 0;
        }
        float GetWeakBoilingSeconds() const {
            return m_weakBoilingSeconds;
        }
        float GetStrongBoilingSeconds() const {
            return m_strongBoilingSeconds;
        }
        bool IsBase() const {
            return m_base;
        }
        float GetQuality() const {
            return m_quality;
        }

      private:
        SDK::wh::entitymodule::C_ItemWrapper *m_itemWrapper; // 0x08
        std::uint32_t m_flags;                               // 0x10
        float m_weakBoilingSeconds;                          // 0x14
        float m_strongBoilingSeconds;                        // 0x18
        std::byte m_opaque001C[0x4];
        bool m_base; // 0x20
        std::byte m_padding0021[0x3];
        float m_quality; // 0x24

        static void AssertLayout() {
            static_assert(offsetof(C_AlchemyResource, m_itemWrapper) == 0x08);
            static_assert(offsetof(C_AlchemyResource, m_flags) == 0x10);
            static_assert(offsetof(C_AlchemyResource, m_weakBoilingSeconds) == 0x14);
            static_assert(offsetof(C_AlchemyResource, m_strongBoilingSeconds) == 0x18);
            static_assert(offsetof(C_AlchemyResource, m_base) == 0x20);
            static_assert(offsetof(C_AlchemyResource, m_quality) == 0x24);
        }
    };

    static_assert(sizeof(C_AlchemyResource) == 0x28);
} // namespace SDK::wh::playermodule
