#pragma once

#include "i_controller.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One track of a `CControllerOptNonVirtual`, stored as
     * `ControllerData`. Both offsets count from the start
     * of this record, so a controller and its keys move together when the
     * controller heap defragments.
     *
     * `CControllerOptNonVirtual::SetRotationKeyTimes`/`SetRotationKeys`
     * (0x18076708C / 0x180767078) and their position twins write exactly
     * these fields; `ControllerData::GetNumCount` (0x1805921C4) reads the key
     * count.
     */
    struct ControllerData {
        /** `eNoFormat`: the controller has no such track. */
        static constexpr std::uint8_t kNoFormat = 0xFF;

        /** `eBitset`: the key times are a bitset whose header carries the key count. */
        static constexpr std::uint8_t kKeyTimesBitset = 6;

        std::int32_t m_nDataOffs;  // 0x00, key times, from this record
        std::int32_t m_nKeysOffs;  // 0x04, keys, from this record
        std::uint16_t m_iCount;    // 0x08, key-time elements
        std::uint8_t m_eTimeFormat;  // 0x0A, EKeyTimesFormat
        std::uint8_t m_eCompressionType; // 0x0B, ECompressionInformation

        bool IsPresent() const {
            return m_eTimeFormat != kNoFormat;
        }

        std::byte *KeyTimes() {
            return reinterpret_cast<std::byte *>(this) + m_nDataOffs;
        }

        std::byte *Keys() {
            return reinterpret_cast<std::byte *>(this) + m_nKeysOffs;
        }

        /** `GetNumCount`: the number of keys, wherever the key-time format keeps it. */
        std::uint32_t KeyCount() {
            if (m_eTimeFormat == kKeyTimesBitset) {
                return *reinterpret_cast<const std::uint16_t *>(KeyTimes() + 4);
            }
            return m_iCount;
        }

        /** Bytes of the key-time track: `m_iCount` elements of the format's size. */
        std::size_t KeyTimesSize() const {
            switch (m_eTimeFormat) {
            case 0: // eF32
            case 3: // eF32StartStop
                return m_iCount * sizeof(float);
            case 1: // eUINT16
            case 4: // eUINT16StartStop
            case kKeyTimesBitset:
                return m_iCount * sizeof(std::uint16_t);
            default: // eByte, eByteStartStop
                return m_iCount;
            }
        }

      private:
        static void AssertLayout() {
            static_assert(offsetof(ControllerData, m_nKeysOffs) == 0x04, "ControllerData::m_nKeysOffs offset mismatch");
            static_assert(offsetof(ControllerData, m_iCount) == 0x08, "ControllerData::m_iCount offset mismatch");
            static_assert(offsetof(ControllerData, m_eTimeFormat) == 0x0A, "ControllerData::m_eTimeFormat offset mismatch");
            static_assert(offsetof(ControllerData, m_eCompressionType) == 0x0B, "ControllerData::m_eCompressionType offset mismatch");
        }
    };

    static_assert(sizeof(ControllerData) == 0xC, "ControllerData size mismatch");

    /**
     * The controller a DBA builds for each (clip, joint): constructed in place,
     * 0x30 apart, by `CInternalDatabaseInfo::ReadController905` (0x180766210)
     * through its constructor at 0x180767058. Table 0x183A506F0; both
     * reference slots are empty, the heap owns it.
     */
    class CControllerOptNonVirtual : public IController {
      public:
        ControllerData &Rotation() {
            return m_rotation;
        }

        ControllerData &Position() {
            return m_position;
        }

        std::uint32_t GetID() const {
            return m_nControllerId;
        }

      private:
        ControllerData m_rotation; // 0x18
        ControllerData m_position; // 0x24

        static void AssertLayout() {
            static_assert(offsetof(CControllerOptNonVirtual, m_rotation) == 0x18, "CControllerOptNonVirtual::m_rotation offset mismatch");
            static_assert(offsetof(CControllerOptNonVirtual, m_position) == 0x24, "CControllerOptNonVirtual::m_position offset mismatch");
        }
    };

    static_assert(sizeof(CControllerOptNonVirtual) == 0x30, "CControllerOptNonVirtual size mismatch");
} // namespace SDK
