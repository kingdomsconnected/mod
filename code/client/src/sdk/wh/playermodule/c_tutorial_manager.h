#pragma once

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /** Player-module controller for named popup and overlay tutorials. */
    class C_TutorialManager {
      public:
        virtual ~C_TutorialManager() = default;
        virtual void Opaque01() = 0;
        virtual void Opaque02() = 0;
        virtual void Opaque03() = 0;

        static C_TutorialManager *GetInstance();

        /** `tutorialName` must address a native CryStringT<char> object. */
        bool InvokeShowTutorialRaw(const void *tutorialName) {
            return hook::this_call<bool>(gPatterns.C_TutorialManager__ShowTutorial, this, tutorialName);
        }

      private:
        std::byte m_tutorialDatabase[0x20]; // 0x08
        void *m_pPopupController;            // 0x28
        void *m_pOverlayController;          // 0x30
        void *m_pTutorialListeners;          // 0x38
        std::uint64_t m_state0040;            // 0x40
        std::uint8_t m_state0048;             // 0x48
        std::byte m_padding0049[0x7];
    };

    static_assert(sizeof(C_TutorialManager) == 0x50,
                  "C_TutorialManager size mismatch");
} // namespace SDK::wh::playermodule
