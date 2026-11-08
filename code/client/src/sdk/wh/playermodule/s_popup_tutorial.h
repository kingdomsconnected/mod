#pragma once

#include "s_tutorial.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /** Reflected native timed popup-tutorial record. */
    struct S_PopupTutorial : S_Tutorial {
        ~S_PopupTutorial() override = default;

      private:
        void Opaque01() override;
        void Opaque02() override;
        void Opaque03() override;

      public:
        CryStringT<char> m_text;             // 0x28, RTTR: Text
        std::uint32_t m_priority;            // 0x30, RTTR: Priority
        bool m_dialogueSync;                 // 0x34, RTTR: DialogueSync
        bool m_disableCloseButton;           // 0x35, RTTR: DisableCloseButton
        std::byte m_padding0036[0x2];
        std::int32_t m_windowSize;            // 0x38, RTTR: WindowSize
        std::byte m_padding003C[0x4];
        CryStringT<char> m_durationOverride; // 0x40, RTTR: DurationOverride
        CryStringT<char> m_overlayLink;       // 0x48, RTTR: OverlayLink
    };

    static_assert(sizeof(S_PopupTutorial) == 0x50,
                  "S_PopupTutorial size mismatch");
} // namespace SDK::wh::playermodule
