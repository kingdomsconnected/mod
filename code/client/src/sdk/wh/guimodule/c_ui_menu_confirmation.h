#pragma once

#include "../framework/c_localized_string.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    struct E_ConfirmationId {
        enum class Type : std::uint8_t {
            Invalid = 0,
            NewGameLostProgress = 1,
            ExitGameLostProgress = 2,
            SaveExitGame = 3,
            SaveResetGame = 4,
            ExitGame = 5,
            LoadSavedGame = 6,
            DeleteSavedGame = 7,
            Reset = 8,
            Apply = 9,
            ApplyWarningUltraSettings = 10,
            BackWarningUltraSettings = 11,
            Back = 12,
            ControlsConflict = 13,
            LanguageNotDownloaded = 14,
            LanguageCantDownloaded = 15,
            LoadFailed = 16,
            GameSaved = 17,
            GameSavedFailed = 18,
            OverwriteSave = 19,
            OverwritePlayline = 20,
            LoadOutdatedSave = 21,
            ApplyWarningUpscaling = 22,
            BackWarningUpscaling = 23,
            SaveManualGame = 24,
            LanguageNotDownloadedFaceAnimNeedRestart = 25,
            FaceAnimNeedRestart = 26,
        };
    };

    /** Reflected native question-and-answer definition used by game menus. */
    class C_UIMenuConfirmation {
      public:
        virtual void Opaque00();
        virtual void Opaque01();
        virtual void Opaque02();

        E_ConfirmationId::Type m_id;             // 0x08, RTTR: Id
        std::byte m_padding0009[0x7];
        framework::C_LocalizedString m_question; // 0x10, RTTR: Question
        framework::C_LocalizedString m_answer0;  // 0x20, RTTR: Answer0
        framework::C_LocalizedString m_answer1;  // 0x30, RTTR: Answer1
        std::int32_t m_defaultAnswer;             // 0x40, RTTR: DefaultAnswer
        std::int32_t m_backAnswer;                // 0x44, RTTR: BackAnswer

      private:
        std::byte m_callback0048[0x40];
        std::byte m_callback0088[0x40];
    };

    static_assert(sizeof(C_UIMenuConfirmation) == 0xC8,
                  "C_UIMenuConfirmation size mismatch");
} // namespace SDK::wh::guimodule
