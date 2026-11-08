#pragma once

#include "c_input_device.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryInput's `CDXInputDevice`, laid out from
     * its constructor 0x180DA24E8, which the keyboard factory 0x182105270
     * calls with the `CDXInput`, the name and the device GUID.
     */
    class CDXInputDevice: public CInputDevice {
      private:
        void *m_dxInput;                // 0x80, CDXInput &
        void *m_pDevice;                // 0x88, LPDIRECTINPUTDEVICE8, ctor null
        const void *m_guid;             // 0x90, const GUID &
        const void *m_pDataFormat;      // 0x98, LPCDIDATAFORMAT, ctor null
        std::uint32_t m_dwCoopLevel;    // 0xA0, ctor 0
        bool m_bNeedsPoll;              // 0xA4, ctor false
        std::byte m_opaque00A5[0x3];    // 0xA5

        static void AssertLayout() {
            static_assert(offsetof(CDXInputDevice, m_dxInput) == 0x80, "CDXInputDevice::m_dxInput offset mismatch");
            static_assert(offsetof(CDXInputDevice, m_guid) == 0x90, "CDXInputDevice::m_guid offset mismatch");
            static_assert(offsetof(CDXInputDevice, m_dwCoopLevel) == 0xA0, "CDXInputDevice::m_dwCoopLevel offset mismatch");
            static_assert(offsetof(CDXInputDevice, m_bNeedsPoll) == 0xA4, "CDXInputDevice::m_bNeedsPoll offset mismatch");
        }
    };

    static_assert(sizeof(CDXInputDevice) == 0xA8, "CDXInputDevice size mismatch");
} // namespace SDK
