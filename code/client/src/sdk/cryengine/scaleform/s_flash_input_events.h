#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Player slot 31 at 0x18056A970. Coordinates truncate to signed 16 bits. */
    struct SFlashCursorEvent {
        enum ECursorState : int {
            eCursorMoved    = 0,
            eCursorPressed  = 1,
            eCursorReleased = 2,
            eWheel          = 3
        };

        SFlashCursorEvent(ECursorState state, int x, int y, int button = 0, float wheel = 0.0f): m_state(state), m_cursorX(x), m_cursorY(y), m_button(button), m_wheelScrollVal(wheel) {}

        ECursorState m_state;
        int m_cursorX;
        int m_cursorY;
        int m_button;
        float m_wheelScrollVal;

      private:
        static void AssertLayout() {
            static_assert(offsetof(SFlashCursorEvent, m_state) == 0x0);
            static_assert(offsetof(SFlashCursorEvent, m_cursorX) == 0x4);
            static_assert(offsetof(SFlashCursorEvent, m_cursorY) == 0x8);
            static_assert(offsetof(SFlashCursorEvent, m_button) == 0xC);
            static_assert(offsetof(SFlashCursorEvent, m_wheelScrollVal) == 0x10);
        }
    };
    static_assert(sizeof(SFlashCursorEvent) == 0x14);

    /** Player slot 32 at 0x182450CB8; key codes are native GFx key codes. */
    struct SFlashKeyEvent {
        enum EKeyState : int {
            eKeyDown = 0,
            eKeyUp   = 1
        };
        enum ESpecialKeyState : unsigned char {
            eShiftPressed  = 0x1,
            eCtrlPressed   = 0x2,
            eAltPressed    = 0x4,
            eCapsToggled   = 0x8,
            eNumToggled    = 0x10,
            eScrollToggled = 0x20
        };

        SFlashKeyEvent(EKeyState state, std::uint32_t keyCode, unsigned char modifiers, unsigned char ascii, std::uint32_t unicode): m_state(state), m_keyCode(keyCode), m_specialKeyState(modifiers), m_asciiCode(ascii), m_wcharCode(unicode) {}

        EKeyState m_state;
        std::uint32_t m_keyCode;
        unsigned char m_specialKeyState;
        unsigned char m_asciiCode;
        std::byte m_opaque000A[0x2] {};
        std::uint32_t m_wcharCode;

      private:
        static void AssertLayout() {
            static_assert(offsetof(SFlashKeyEvent, m_state) == 0x0);
            static_assert(offsetof(SFlashKeyEvent, m_keyCode) == 0x4);
            static_assert(offsetof(SFlashKeyEvent, m_specialKeyState) == 0x8);
            static_assert(offsetof(SFlashKeyEvent, m_asciiCode) == 0x9);
            static_assert(offsetof(SFlashKeyEvent, m_wcharCode) == 0xC);
        }
    };
    static_assert(sizeof(SFlashKeyEvent) == 0x10);

    /** Player slot 33 at 0x182450B1C. Unicode character, not physical key. */
    struct SFlashCharEvent {
        explicit SFlashCharEvent(std::uint32_t unicode, unsigned char keyboard = 0): m_wCharCode(unicode), m_keyboardIndex(keyboard) {}

        std::uint32_t m_wCharCode;
        unsigned char m_keyboardIndex;
        std::byte m_opaque0005[0x3] {};

      private:
        static void AssertLayout() {
            static_assert(offsetof(SFlashCharEvent, m_wCharCode) == 0x0);
            static_assert(offsetof(SFlashCharEvent, m_keyboardIndex) == 0x4);
        }
    };
    static_assert(sizeof(SFlashCharEvent) == 0x8);
} // namespace SDK
