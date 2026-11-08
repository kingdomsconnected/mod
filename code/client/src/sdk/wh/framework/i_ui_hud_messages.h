#pragma once

#include <cstddef>

namespace SDK {
    template <typename Character> struct CryStringT;
}

namespace SDK::wh::framework {
    /** Native HUD message/notification interface used by the GUI module. */
    class I_UIHudMessages {
      public:
        /**
         * The single-line info text above the HUD, held for `duration`
         * milliseconds. `priority` decides what an already-showing message
         * does to this one.
         */
        void InvokeShowInfoTextRaw(const CryStringT<char> &message,
                                   int duration,
                                   int priority) {
            ShowInfoText(message, duration, priority);
        }

        void InvokeHideInfoTextRaw() { HideInfoText(); }

      private:
        virtual void ShowInfoText(const CryStringT<char> &message,
                                  int duration,
                                  int priority) = 0;
        virtual void HideInfoText() = 0;
        virtual void Opaque02() = 0;
        virtual void Opaque03() = 0;
        virtual void Opaque04() = 0;
        virtual void Opaque05() = 0;
        virtual void Opaque06() = 0;
    };

    static_assert(sizeof(I_UIHudMessages) == 0x8,
                  "I_UIHudMessages size mismatch");
} // namespace SDK::wh::framework
