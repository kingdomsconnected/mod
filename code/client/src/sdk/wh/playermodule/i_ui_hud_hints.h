#pragma once

#include "../../patterns.h"

#include <cstdint>

namespace SDK::wh::playermodule {
    /** Native HUD hint, tooltip, progress, and QAM-arrow presentation contract. */
    class I_UIHudHints {
      public:
        static I_UIHudHints *GetInstance();

        /** Replaces the current native action-hint collection through slot 0. */
        void InvokeSetActionHintsRaw(const void *hints, std::uint32_t state) {
            Opaque00(hints, state);
        }

        /** Dispatches the progress operation at slot 1. */
        void InvokeSetProgressRaw(float progress, int value0, int value1) {
            Opaque01(progress, value0, value1);
        }

        /** Shows a native CryStringT-backed tooltip through slot 2. */
        void InvokeShowTooltipRaw(const void *text, int value0, int value1) {
            Opaque02(text, value0, value1);
        }

        /** Dispatches the two-integer tooltip update operation at slot 3. */
        void InvokeUpdateTooltipRaw(int value0, int value1) {
            Opaque03(value0, value1);
        }

        /** Hides the currently displayed tooltip through slot 4. */
        void InvokeHideTooltipRaw() { Opaque04(); }

        /** Hides the currently displayed QAM arrows through slot 6. */
        void InvokeHideQAMArrowsRaw() { Opaque06(); }

      private:
        virtual void Opaque00(const void *hints, std::uint32_t state) = 0;
        virtual void Opaque01(float progress, int value0, int value1) = 0;
        virtual void Opaque02(const void *text, int value0, int value1) = 0;
        virtual void Opaque03(int value0, int value1) = 0;
        virtual void Opaque04() = 0;
        virtual void Opaque05() = 0;
        virtual void Opaque06() = 0;
        virtual void Opaque07() = 0;
        virtual void Opaque08() = 0;
        virtual void Opaque09() = 0;
    };

    static_assert(sizeof(I_UIHudHints) == 0x8,
                  "I_UIHudHints size mismatch");
} // namespace SDK::wh::playermodule
