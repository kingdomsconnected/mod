#pragma once

#include "../entitymodule/c_item.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::xgenaimodule {
    struct SpawnedItemVectorLayout {
        entitymodule::C_Item **m_pData;
        std::size_t m_size;
        std::size_t m_capacity;
    };

    static_assert(sizeof(SpawnedItemVectorLayout) == 0x18,
                  "SpawnedItemVectorLayout size mismatch");

    /** Holder used to track items created by scripted spawn events. */
    class C_SpawnedItemsHolder : public entitymodule::C_ItemHolder {
      public:
        ~C_SpawnedItemsHolder() override = default;

        std::size_t GetPrimaryItemCount() const {
            return m_primaryItems.m_size;
        }

        entitymodule::C_Item **GetPrimaryItemsData() {
            return m_primaryItems.m_pData;
        }

        entitymodule::C_Item *const *GetPrimaryItemsData() const {
            return m_primaryItems.m_pData;
        }

        std::size_t GetSecondaryItemCount() const {
            return m_secondaryItems.m_size;
        }

        entitymodule::C_Item **GetSecondaryItemsData() {
            return m_secondaryItems.m_pData;
        }

        entitymodule::C_Item *const *GetSecondaryItemsData() const {
            return m_secondaryItems.m_pData;
        }

        bool GetState0038Raw() const { return m_state0038 != 0; }
        bool GetState0039Raw() const { return m_state0039 != 0; }

      private:
        SpawnedItemVectorLayout m_primaryItems;   // 0x08
        SpawnedItemVectorLayout m_secondaryItems; // 0x20
        std::uint8_t m_state0038;                 // 0x38
        std::uint8_t m_state0039;                 // 0x39
        std::byte m_padding003A[0x6];             // 0x3A
    };

    static_assert(sizeof(C_SpawnedItemsHolder) == 0x40,
                  "C_SpawnedItemsHolder size mismatch");
} // namespace SDK::wh::xgenaimodule
