#pragma once

namespace SDK {
    /** CFlashUIElement's fourth base; native slots verified at 0x184049B80. */
    struct IVirtualKeyboardEvents {
        virtual ~IVirtualKeyboardEvents()               = default;
        virtual void KeyboardCancelled()                = 0;
        virtual void KeyboardFinished(const char *text) = 0;
    };

    static_assert(sizeof(IVirtualKeyboardEvents) == 0x8, "IVirtualKeyboardEvents size mismatch");
} // namespace SDK
