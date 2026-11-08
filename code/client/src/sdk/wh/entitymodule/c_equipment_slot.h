#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    struct EquipmentSlotUIntVectorLayout {
        std::uint32_t *m_pBegin;
        std::uint32_t *m_pEnd;
        std::uint32_t *m_pCapacityEnd;
    };

    struct EquipmentSlotPointerVectorLayout {
        void **m_pBegin;
        void **m_pEnd;
        void **m_pCapacityEnd;
    };

    static_assert(sizeof(EquipmentSlotUIntVectorLayout) == 0x18,
                  "EquipmentSlotUIntVectorLayout size mismatch");
    static_assert(sizeof(EquipmentSlotPointerVectorLayout) == 0x18,
                  "EquipmentSlotPointerVectorLayout size mismatch");

    /** Reflected equipment-slot definition stored by the slot database. */
    class C_EquipmentSlot {
      public:
        virtual ~C_EquipmentSlot() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;

        std::int32_t GetSlotIdRaw() const { return m_slotId; }
        const char *GetString0008Raw() const { return m_pString0008; }
        const char *GetString0010Raw() const { return m_pString0010; }

        std::size_t GetUIntValueCount() const {
            return m_uintValues.m_pBegin != nullptr
                       ? static_cast<std::size_t>(m_uintValues.m_pEnd -
                                                  m_uintValues.m_pBegin)
                       : 0;
        }

      private:
        char *m_pString0008;                       // 0x08, CryStringT<char>
        char *m_pString0010;                       // 0x10, CryStringT<char>
        std::int32_t m_slotId;                     // 0x18
        std::uint32_t m_state001C;                 // 0x1C
        std::uint32_t m_state0020;                 // 0x20
        std::uint32_t m_state0024;                 // 0x24
        std::uint32_t m_state0028;                 // 0x28
        std::byte m_padding002C[0x4];              // 0x2C
        EquipmentSlotUIntVectorLayout m_uintValues; // 0x30
        char *m_pString0048;                       // 0x48, CryStringT<char>
        char *m_pString0050;                       // 0x50, CryStringT<char>
        std::uint64_t m_state0058;                 // 0x58
        std::uint64_t m_state0060;                 // 0x60
        EquipmentSlotPointerVectorLayout m_values; // 0x68
    };

    static_assert(sizeof(C_EquipmentSlot) == 0x80,
                  "C_EquipmentSlot size mismatch");
} // namespace SDK::wh::entitymodule
