#pragma once

#include "../framework/c_localized_string.h"
#include "s_tutorial.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /** Reflected native full-screen tutorial-overlay record. */
    struct S_OverlayTutorial : S_Tutorial {
        ~S_OverlayTutorial() override = default;

      private:
        void Opaque01() override;
        void Opaque02() override;
        void Opaque03() override;

      public:
        framework::C_LocalizedString m_uiName; // 0x28, RTTR: UiName
        std::int32_t m_uiOrder;                // 0x38, RTTR: UiOrder
        std::byte m_padding003C[0x4];
        CryStringT<char> m_uiScreenName; // 0x40, RTTR: UiScreenName
        bool m_visibleInMenu;            // 0x48, RTTR: VisibleInMenu
        bool m_isSystemOverlay;           // 0x49, RTTR: IsSystemOverlay
        std::byte m_padding004A[0x6];
    };

    static_assert(sizeof(S_OverlayTutorial) == 0x50,
                  "S_OverlayTutorial size mismatch");
} // namespace SDK::wh::playermodule
