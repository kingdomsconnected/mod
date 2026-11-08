#pragma once

#include "i_input.h"
#include "i_input_device.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryInput's `CInputDevice`, vtable
     * `CInputDevice::vftable`, laid out from its constructor 0x180DA2550.
     * KCD2 keeps CryEngine's member order but carries an unexplained run
     * between the device id and the enabled flag.
     */
    class CInputDevice: public IInputDevice {
      private:
        EInputDeviceType m_deviceType;        // 0x08, ctor 0xFF (eIDT_Unknown); each device overwrites it
        std::uint8_t m_deviceId;              // 0x09, ctor 4; CDXInputDevice resets it to 0
        std::byte m_opaque000A[0xF];          // 0x0A
        bool m_enabled;                       // 0x19, ctor true
        std::byte m_opaque001A[0x6];          // 0x1A
        IInput *m_input;                      // 0x20, the input system that created the device
        const char *m_deviceName;             // 0x28, CryStringT<char> payload ("keyboard")
        std::byte m_nameToId[0x10];           // 0x30, std::map<TKeyName, uint32>
        std::byte m_nameToInfo[0x10];         // 0x40, std::map<TKeyName, SInputSymbol *>
        std::byte m_idToInfo[0x10];           // 0x50, std::map<EKeyId, SInputSymbol *>
        std::byte m_devSpecIdToSymbol[0x10];  // 0x60, std::map<uint32, SInputSymbol *>
        void *m_lock;                         // 0x70, SRWLOCK
        void *m_opaque0078;                   // 0x78, ctor null, written with the lock

        static void AssertLayout() {
            static_assert(offsetof(CInputDevice, m_deviceType) == 0x08, "CInputDevice::m_deviceType offset mismatch");
            static_assert(offsetof(CInputDevice, m_deviceId) == 0x09, "CInputDevice::m_deviceId offset mismatch");
            static_assert(offsetof(CInputDevice, m_enabled) == 0x19, "CInputDevice::m_enabled offset mismatch");
            static_assert(offsetof(CInputDevice, m_input) == 0x20, "CInputDevice::m_input offset mismatch");
            static_assert(offsetof(CInputDevice, m_deviceName) == 0x28, "CInputDevice::m_deviceName offset mismatch");
            static_assert(offsetof(CInputDevice, m_nameToId) == 0x30, "CInputDevice::m_nameToId offset mismatch");
            static_assert(offsetof(CInputDevice, m_devSpecIdToSymbol) == 0x60, "CInputDevice::m_devSpecIdToSymbol offset mismatch");
            static_assert(offsetof(CInputDevice, m_lock) == 0x70, "CInputDevice::m_lock offset mismatch");
        }
    };

    static_assert(sizeof(CInputDevice) == 0x80, "CInputDevice size mismatch");
} // namespace SDK
