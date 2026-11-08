#pragma once

#include "c_dx_input_device.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryInput's keyboard. Its key names (`q`, `tab`) are DirectInput
     * scancodes, so they never move with the layout; what a key is called
     * on screen comes from a global per-scancode character table
     * `ChangeLanguage` builds for one layout, and which every native label
     * reads -- `GetOSKeyName`, `GetInputCharUnicode`, the string table's
     * `@cc_<key>`.
     */
    class CKeyboard: public CDXInputDevice {
      public:
        /**
         * 0x1813B784C. Records `layout` (an HKL) and rebuilds the character
         * table for it. The engine calls it only from `Init`, with the init
         * thread's layout, and never again when the player's layout differs
         * or changes, so the table can name keys for a layout nobody uses.
         */
        void ChangeLanguage(void *layout);

        /** The HKL the table was last built for. */
        void *GetLayout() const {
            return m_layout;
        }

      private:
        std::uint32_t m_baseflags;    // 0xA8, ctor 6 (DISCL_NONEXCLUSIVE | DISCL_FOREGROUND), factory 0x182105270
        std::byte m_opaque00AC[0x4];  // 0xAC
        void *m_layout; // 0xB0

        static void AssertLayout() {
            static_assert(offsetof(CKeyboard, m_baseflags) == 0xA8, "CKeyboard::m_baseflags offset mismatch");
            static_assert(offsetof(CKeyboard, m_layout) == 0xB0, "CKeyboard::m_layout offset mismatch");
        }
    };

    static_assert(sizeof(CKeyboard) == 0xB8, "CKeyboard size mismatch");
} // namespace SDK
