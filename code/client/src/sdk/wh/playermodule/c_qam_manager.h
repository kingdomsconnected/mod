#pragma once

#include "i_qam_manager.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * One native quick-access slot. QAM setup and iteration advance these
     * records in exact 0x30-byte strides. Torch drawing instead resolves the
     * equipment manager's special-item cache entry at +0xC8.
     */
    struct C_QAMSlot {
        entitymodule::C_Item *GetItem(std::uint32_t index) const {
            return index < 2 ? m_pItems[index] : nullptr;
        }

      private:
        entitymodule::C_Item *m_pItems[2]; // 0x00
        // Constructor sub_1816A86C0 zeroes both pointers and copies the zero xmmword_183F74F98 into each block.
        std::byte m_value0010[0x10]; // 0x10
        std::byte m_value0020[0x10]; // 0x20

        static void AssertLayout() {
            static_assert(offsetof(C_QAMSlot, m_pItems) == 0x00, "C_QAMSlot::m_pItems offset mismatch");
        }
    };

    static_assert(sizeof(C_QAMSlot) == 0x30, "C_QAMSlot size mismatch");

    /** Concrete quick-access-menu equipment state embedded in each outfit. */
    class C_QAMManager: public I_QAMManager {
      public:
        ~C_QAMManager() override = default;

        void unk_fn0001() override;
        void unk_fn0002() override;
        void unk_fn0003() override;
        void unk_fn0004() override;
        void SelectSlot(std::uint32_t slot, bool removePreviousEquipment) override;
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
        void SetItem(entitymodule::C_Item *item, std::uint32_t slot) override;
        void unk_fn0023() override;
        void unk_fn0024() override;
        void unk_fn0025() override;
        void ClearAllSlots() override;
        void unk_fn0027() override;
        void SynchronizeSlotsFromEquipment() override;
        void unk_fn0029() override;
        void unk_fn0030() override;
        void unk_fn0031() override;
        void unk_fn0032() override;
        void unk_fn0033() override;
        void unk_fn0034() override;
        void unk_fn0035() override;
        void unk_fn0036() override;
        void unk_fn0037() override;
        void unk_fn0038() override;

        std::uint32_t GetActiveSlotIndex() const {
            return m_activeSlotIndex;
        }
        std::uint32_t GetSlotCount() const {
            return m_slotCount;
        }

        static constexpr std::uint32_t kSlotCapacity = 4;

        C_QAMSlot *GetSlot(std::uint32_t index) {
            return index < m_slotCount && index < kSlotCapacity ? &m_slots[index] : nullptr;
        }

        const C_QAMSlot *GetSlot(std::uint32_t index) const {
            return index < m_slotCount && index < kSlotCapacity ? &m_slots[index] : nullptr;
        }

        C_QAMSlot *GetActiveSlot() {
            return GetSlot(m_activeSlotIndex);
        }
        const C_QAMSlot *GetActiveSlot() const {
            return GetSlot(m_activeSlotIndex);
        }

        void *GetOwnerContext() const {
            return m_pOwnerContext;
        }

      private:
        std::byte m_handle0008[0x10];
        std::byte m_handle0018[0x10];
        std::uint32_t m_state0028;        // 0x28
        std::byte m_padding002C[0x4];     // 0x2C
        C_QAMSlot m_slots[kSlotCapacity]; // 0x30
        std::uint32_t m_activeSlotIndex;  // 0xF0
        std::uint32_t m_slotCount;        // 0xF4
        void *m_pOwnerContext;            // 0xF8

        static void AssertLayout() {
            static_assert(offsetof(C_QAMManager, m_state0028) == 0x28, "C_QAMManager::m_state0028 offset mismatch");
            static_assert(offsetof(C_QAMManager, m_slots) == 0x30, "C_QAMManager::m_slots offset mismatch");
            static_assert(offsetof(C_QAMManager, m_activeSlotIndex) == 0xF0, "C_QAMManager::m_activeSlotIndex offset mismatch");
            static_assert(offsetof(C_QAMManager, m_slotCount) == 0xF4, "C_QAMManager::m_slotCount offset mismatch");
            static_assert(offsetof(C_QAMManager, m_pOwnerContext) == 0xF8, "C_QAMManager::m_pOwnerContext offset mismatch");
        }
    };

    static_assert(sizeof(C_QAMManager) == 0x100, "C_QAMManager size mismatch");
} // namespace SDK::wh::playermodule
