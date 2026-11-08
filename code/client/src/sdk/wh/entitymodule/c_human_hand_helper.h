#pragma once

#include "i_equipment_manager_listener.h"
#include "i_human_hand_helper.h"
#include "i_inventory_listener.h"
#include "i_item_attachment_listener.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Human;

    /**
     * Humanoid hand/equipment bridge owned by C_Human.
     *
     * RTTI proves I_EquipmentManagerListener at +0x00,
     * I_ItemAttachmentListener at +0x08, I_HumanHandHelper at +0x10, and
     * I_InventoryListener at +0x18. C_Human's constructor allocates exactly
     * 0xB0 bytes and stores the owning human at +0x20.
     */
    class C_HumanHandHelper : public I_EquipmentManagerListener,
                              public I_ItemAttachmentListener,
                              public I_HumanHandHelper,
                              public I_InventoryListener {
      public:
        ~C_HumanHandHelper() override = default;

        C_Human *GetHuman() const { return m_pHuman; }

        I_HumanHandHelper *GetHandInterface() {
            return static_cast<I_HumanHandHelper *>(this);
        }

      private:
        C_Human *m_pHuman; // 0x20
        // Factory 0x1804CE428.
        void *m_values0028[6];          // 0x28, factory zeroes all six
        std::uint64_t m_itemId0058;     // 0x58, factory seeds the invalid WUID (qword_185324AC8, -1)
        bool m_state0060;               // 0x60, factory clears it
        std::byte m_padding0061[0x7];
        std::byte m_state0068[0x28];    // 0x68, built by 0x1804CE554 (through 0x1804CE520)
        std::uint32_t m_value0090;      // 0x90, 0x1804CE520 zeroes
        bool m_state0094;               // 0x94, 0x1804CE520 clears
        std::byte m_padding0095[0x3];
        std::uint64_t m_value0098;      // 0x98, 0x1804CE520 zeroes
        std::uint32_t m_value00A0;      // 0xA0, 0x1804CE520 zeroes
        float m_value00A4;              // 0xA4, 0x1804CE520 sets 1.0
        std::uint64_t m_value00A8;      // 0xA8, 0x1804CE520 zeroes

        static void AssertLayout() {
            static_assert(offsetof(C_HumanHandHelper, m_itemId0058) == 0x58, "C_HumanHandHelper::m_itemId0058 offset mismatch");
            static_assert(offsetof(C_HumanHandHelper, m_state0068) == 0x68, "C_HumanHandHelper::m_state0068 offset mismatch");
            static_assert(offsetof(C_HumanHandHelper, m_value00A4) == 0xA4, "C_HumanHandHelper::m_value00A4 offset mismatch");
            static_assert(offsetof(C_HumanHandHelper, m_value00A8) == 0xA8, "C_HumanHandHelper::m_value00A8 offset mismatch");
            static_assert(offsetof(C_HumanHandHelper, m_pHuman) == 0x20,
                          "C_HumanHandHelper::m_pHuman offset mismatch");
        }
    };

    static_assert(sizeof(C_HumanHandHelper) == 0xB0,
                  "C_HumanHandHelper size mismatch");
} // namespace SDK::wh::entitymodule
