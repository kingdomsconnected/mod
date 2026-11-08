#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    class C_UIMenu;

    /**
     * The GUI module's save/load and game-start controller.
     *
     * Retail New Game, the "New Game (debug)" entries, Continue, Load Game and
     * `+map` all funnel through StartNewGame. Only the members that path uses
     * are mapped; the rest of the object, vtables included, is padding.
     */
    class C_UISaveLoad final {
      public:
        /**
         * Stamped into the flow byte before a start is queued. StartNewGame
         * turns NewGameDebug and CommandLineMap into the debug flag that
         * C_NewGameHelper records for wh::rpgmodule::IsNewGameModeDebug, and
         * that suppresses the new-game intro through wh_sys_HideIntroVideo bit
         * 0x1000. NewGame is the retail story start and produces neither.
         */
        enum class E_StartFlow : std::uint8_t {
            NewGameDebug   = 2,
            NewGame        = 3,
            CommandLineMap = 5,
        };

        /** True while a start is queued; the UI refuses to queue another. */
        bool IsStartPending() const {
            return m_startPending != 0;
        }

        /**
         * `startMode` is 1 at every native call site, `modList` is a
         * semicolon-separated list that both start paths leave empty, and
         * `levelName` may be null to let the playline choose. All three are
         * copied into engine-owned storage before the call returns.
         */
        void InvokeStartNewGameRaw(std::uint32_t startMode, const char *modList, const char *levelName);

        /**
         * Emits one row of the load page: `AddLoadButton(rowId, mode, label,
         * tooltip, enabled)`. The movie's only variable-length list widget.
         *
         * Both integers are echoed back through `OnLoadButton` in order, so
         * the pair is the row's identity and implies nothing about a savegame.
         *
         * Every argument travels by const reference into the engine's argument
         * packer, so the strings have to be ones the engine built.
         */
        void InvokeAddLoadButton(std::int32_t rowId, std::int32_t mode, const CryStringT<char> &label, const CryStringT<char> &tooltip, bool enabled);

        C_UIMenu *GetMenu() const {
            return m_pMenu;
        }

        // Layout, not API: an overlay on the native object, public only so the
        // offset assertions can see it.
        std::byte m_padding0000[0x68];
        void *m_pUiState;             // 0x68, selected playline at +0x48 -> +0x188
        std::byte m_padding0070[0x2]; // 0x70
        std::uint8_t m_startPending;  // 0x72
        std::uint8_t m_startFlow;     // 0x73
        std::byte m_padding0074[0xAC];
        C_UIMenu *m_pMenu; // 0x120, the root menu this page draws into
    };

    static_assert(offsetof(C_UISaveLoad, m_pMenu) == 0x120, "C_UISaveLoad owning-menu offset mismatch");

    static_assert(offsetof(C_UISaveLoad, m_pUiState) == 0x68, "C_UISaveLoad UI state offset mismatch");
    static_assert(offsetof(C_UISaveLoad, m_startPending) == 0x72, "C_UISaveLoad start-pending offset mismatch");
    static_assert(offsetof(C_UISaveLoad, m_startFlow) == 0x73, "C_UISaveLoad start-flow offset mismatch");
} // namespace SDK::wh::guimodule
