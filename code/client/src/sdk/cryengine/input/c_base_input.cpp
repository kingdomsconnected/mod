#include "c_base_input.h"

namespace SDK {
    CBaseInput *CBaseInput::GetInstance() {
        return static_cast<CBaseInput *>(IInput::GetInstance());
    }

    CKeyboard *CBaseInput::GetKeyboard() const {
        for (const std::shared_ptr<IInputDevice> &device : m_inputDevices) {
            if (device != nullptr && device->GetDeviceType() == EInputDeviceType::Keyboard) {
                return static_cast<CKeyboard *>(device.get());
            }
        }
        return nullptr;
    }
} // namespace SDK
