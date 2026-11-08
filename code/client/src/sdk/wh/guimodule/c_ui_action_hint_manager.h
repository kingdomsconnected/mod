#pragma once

#include "../playermodule/i_ui_action_hint_manager.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /** GUI-side action-hint presenter owned directly by C_GUIModule. */
    class C_UIActionHintManager: public playermodule::I_UIActionHintManager {
      public:
        ~C_UIActionHintManager() override = default;

        static C_UIActionHintManager *GetInstance();

        std::uint32_t GetStateRaw() const {
            return m_state;
        }

      private:
        void Opaque00() override;
        void Opaque01() override;
        void Opaque02() override;
        void Opaque03() override;

        void *m_pSecondaryVTable;  // 0x08
        void *m_pActionMapState;   // 0x10
        std::uint16_t m_actionId0; // 0x18
        std::uint16_t m_actionId1; // 0x1A
        std::byte m_padding001C[0x4];
        std::uint32_t m_state; // 0x20
        std::byte m_opaque0024[0xC];
        void *m_pState; // 0x30
    };

    static_assert(sizeof(C_UIActionHintManager) == 0x38, "C_UIActionHintManager size mismatch");
} // namespace SDK::wh::guimodule
