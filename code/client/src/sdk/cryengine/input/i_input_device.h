#pragma once

#include <cstdint>

namespace SDK {
    /** The native input device categories. */
    enum class EInputDeviceType : std::uint8_t {
        Keyboard         = 0,
        Mouse            = 1,
        Joystick         = 2,
        EyeTracker       = 3,
        Gamepad          = 4,
        MotionController = 5,
        Unknown          = 0xFF,
    };

    /** One CryInput device, as `CBaseInput` keeps them; CKeyboard's table is 0x183AA4748. */
    class IInputDevice {
      public:
        virtual ~IInputDevice()                        = default; // 0
        virtual void Opaque01()                        = 0;       // GetDeviceName
        virtual EInputDeviceType GetDeviceType() const = 0;       // 2, the byte at +0x08
    };

    static_assert(sizeof(IInputDevice) == 0x8, "IInputDevice interface size mismatch");
} // namespace SDK
