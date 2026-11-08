#pragma once

#include "e_hud_elements.h"

namespace SDK::wh::framework {
    /** Secondary base at C_UIHudMask+0x58; RTTI table 0x183C13D60. */
    class I_UIHudMask {
      public:
        virtual ~I_UIHudMask() = default;

        /** Slot 1, called by C_UIHudMask::ApplyElementVisibility (0x180555A10). */
        virtual bool IsElementVisible(E_HudElements::Type element) const = 0;

        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
    };

    static_assert(sizeof(I_UIHudMask) == 0x8);
} // namespace SDK::wh::framework
