#pragma once

#include "c_keyboard.h"
#include "i_input.h"
#include "i_input_device.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace SDK {
    /**
     * CryInput's shared implementation under `CDXInput`, laid out from the
     * device walk its `GetOSKeyName` (0x1812D044C) makes over +0x110, and
     * from its constructor 0x180DA3418 (RTTI `Cry::Input::CSystem`).
     */
    class CBaseInput: public IInput {
      public:
        /** The keyboard among the devices, or null before it is created. */
        CKeyboard *GetKeyboard() const;

        static CBaseInput *GetInstance();

      private:
        bool (*m_onFilterInputEvent)(SInputEvent *);     // 0x08, OnFilterInputEvent; ctor 0x180DA3418 sets it last (0x18041A6A0)
        void *m_systemEventListenerVTable;             // 0x10, second base (ISystemEventListener)
        std::byte m_opaque0018[0x10];                  // 0x18, built by 0x1808DDC28
        std::byte m_holdSymbols[0x18];                 // 0x28, std::vector<SInputSymbol *>, ctor reserves 64
        std::byte m_opaque0040[0x98];                  // 0x40, the listener sets and exclusive listener (layout diverges from CryEngine)
        bool m_enableEventPosting;                     // 0xD8, ctor true
        bool m_retriggering;                           // 0xD9, ctor false
        std::byte m_opaque00DA[0x6];                   // 0xDA
        void *m_postInputEventMutex;                   // 0xE0, SRWLOCK
        std::byte m_opaque00E8[0x28];                  // 0xE8
        std::vector<std::shared_ptr<IInputDevice>> m_inputDevices; // 0x110
        std::byte m_opaque0128[0xC];                   // 0x128, a lock the ctor takes around the device vector
        std::int32_t m_forceFeedbackDeviceIndex;       // 0x134, ctor -1 (EFF_INVALID_DEVICE_INDEX)
        std::byte m_inputBlockData[0x10];              // 0x138, std::list<SInputBlockData>
        std::int32_t m_modifiers;                      // 0x148, ctor 0
        std::byte m_opaque014C[0x4];                   // 0x14C
        void *m_pCVars;                                // 0x150, CInputCVars *, ctor allocates 0x70 bytes

        static void AssertLayout() {
            // Three pointers, as the game's release vector is.
            static_assert(sizeof(std::vector<std::shared_ptr<IInputDevice>>) == 0x18, "std::vector layout does not match the game's");
            static_assert(offsetof(CBaseInput, m_onFilterInputEvent) == 0x08, "CBaseInput::m_onFilterInputEvent offset mismatch");
            static_assert(offsetof(CBaseInput, m_holdSymbols) == 0x28, "CBaseInput::m_holdSymbols offset mismatch");
            static_assert(offsetof(CBaseInput, m_enableEventPosting) == 0xD8, "CBaseInput::m_enableEventPosting offset mismatch");
            static_assert(offsetof(CBaseInput, m_postInputEventMutex) == 0xE0, "CBaseInput::m_postInputEventMutex offset mismatch");
            static_assert(offsetof(CBaseInput, m_inputDevices) == 0x110, "CBaseInput::m_inputDevices offset mismatch");
            static_assert(offsetof(CBaseInput, m_forceFeedbackDeviceIndex) == 0x134, "CBaseInput::m_forceFeedbackDeviceIndex offset mismatch");
            static_assert(offsetof(CBaseInput, m_inputBlockData) == 0x138, "CBaseInput::m_inputBlockData offset mismatch");
            static_assert(offsetof(CBaseInput, m_modifiers) == 0x148, "CBaseInput::m_modifiers offset mismatch");
            static_assert(offsetof(CBaseInput, m_pCVars) == 0x150, "CBaseInput::m_pCVars offset mismatch");
        }
    };
} // namespace SDK
