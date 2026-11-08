#pragma once

#include "../../patterns.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The settings pages of the root menu, owned by value inside C_UIMenu at
     * +0x1D8 and built by `C_UISettings::C_UISettings` (0x180600544).
     *
     * Each page is a `ShowXxxPage` that calls SetPage, emits one flash row
     * per setting and ends in `C_UIMenu::ShowPage`. The rows are emitted by
     * the C_UIMenuBase natives below, which take the row id as a string: a
     * row the E_ButtonId table has never heard of is drawn like any other,
     * and its events come back through OnInteractiveValue/OnInteractiveChoice
     * by that string, where the game's own handlers ignore a spelling
     * `E_ButtonId::FromString` does not know.
     *
     * It is also an IPlayerProfileListener, at +0x58: `LoadFromProfile`
     * (0x18079C4A8) and `SaveToProfile` (0x18079CF88) are how every value on
     * these pages reaches `attributes.xml`.
     *
     * Game thread only.
     */
    class C_UISettings final {
      public:
        C_UISettings()                                = delete;
        C_UISettings(const C_UISettings &)            = delete;
        C_UISettings &operator=(const C_UISettings &) = delete;

        /**
         * A slider row. The label and tooltip are localized when they start
         * with '@' and emitted as they are otherwise. `container` is the
         * menu_buttons Container, 0 for a row in the page's list.
         */
        void AddValueButton(const CryStringT<char> &id, const CryStringT<char> &label, bool enabled, float value, float min, float max, const char *tooltip, int container = 0) {
            // By value in the native's signature, so it is the native's to destroy.
            CryStringT<char> ownedTooltip = ConstructCryString(tooltip);
            hook::this_call<void>(gPatterns.C_UIMenuBase__AddValueButton, this, &id, &label, enabled, value, min, max, &ownedTooltip, container);
        }

        /** A row of mutually exclusive options, which AddChoiceOption then fills. */
        void AddChoicesButton(const CryStringT<char> &id, const CryStringT<char> &label, bool enabled, const char *tooltip, int container = 0) {
            CryStringT<char> ownedTooltip = ConstructCryString(tooltip);
            hook::this_call<void>(gPatterns.C_UIMenuBase__AddChoicesButton, this, &id, &label, enabled, &ownedTooltip, container);
        }

        /**
         * Option `index` of a choices row, 0-based. The label reaches the movie as it is given, so
         * it must already be a marked key (`@ui_yes`): the game's own rows pass Localize's output
         * (sub_181F8BA20), and plain text is never displayed.
         */
        void AddChoiceOption(const CryStringT<char> &id, int index, const CryStringT<char> &label, bool enabled, const CryStringT<char> &tooltip, int container = 0) {
            // The last parameter is the row's container. Its mangled type is bool, but the native
            // reads four bytes there (`mov edx, [rdi]` in sub_181F81A90) and the game's own rows pass
            // the menu_buttons Container column, so it goes over as an int: a bool leaves three bytes
            // of stack in it, and the movie then looks for the row in no container at all.
            hook::this_call<void>(gPatterns.C_UIMenuBase__AddChoiceOption, this, &id, index, &label, enabled, &tooltip, container);
        }

        void SetValue(const CryStringT<char> &id, float value, int container = 0) {
            hook::this_call<void>(gPatterns.C_UIMenuBase__SetValue, this, &id, value, container);
        }

        void SetChoice(const CryStringT<char> &id, int index) {
            hook::this_call<void>(gPatterns.C_UIMenuBase__SetChoice, this, &id, index, 0);
        }

      private:
        void *m_pVTable;                // 0x000, C_UIMenuBase
        // Constructor 0x180600544. +0x08..+0x58 is the C_UIFlashBase layer
        // under C_UIMenuBase (sub_180BC7F58); the ICVar slots hold the cvar
        // named beside them.
        std::byte m_flashBase0008[0x50];   // 0x008; second vftable at +0x10, element name at +0x50
        void *m_pProfileListenerVTable; // 0x058, IPlayerProfileListener
        const void *m_pVTable0060;         // 0x060; C_UISettings
        std::byte m_opaque0068[0xC0];      // 0x068; not written by the constructor
        void *m_pSubtitlesEnabled;   // ICVar, 0x128, wh_ui_SubtitlesEnabled
        void *m_pBubblesEnabled;     // ICVar, 0x130, wh_ui_BubblesEnabled
        void *m_pTutorialsEnabled;   // ICVar, 0x138, wh_ui_TutorialsEnabled
        void *m_pRoadMagnetism;      // ICVar, 0x140, wh_horse_RoadMagnetism
        void *m_pShowNormalCursor;   // ICVar, 0x148, wh_ui_ShowNormalCursor
        void *m_pInputDevice;        // ICVar, 0x150, wh_ui_InputDevice
        void *m_pPreferredInputDevice; // ICVar, 0x158, wh_ui_PreferredInputDevice
        std::uint32_t m_value0160;         // 0x160; ctor stores sub_1809EAEF8()
        std::uint32_t m_value0164;         // 0x164; ctor stores sub_1819D55FC()
        void *m_pEnableSubtitleSpeakerName; // ICVar, 0x168, wh_ui_EnableSubtitleSpeakerName
        void *m_pEnableSubtitleBg;   // ICVar, 0x170, wh_ui_EnableSubtitleBg
        void *m_pSubtitleSize;       // ICVar, 0x178, wh_ui_SubtitleSize
        void *m_pTelemetryMarketingConsent; // ICVar, 0x180, wh_rpg_TelemetryMarketingConsent
        void *m_pEnableAdaptiveHud;  // ICVar, 0x188, wh_ui_EnableAdaptiveHud
        void *m_pWidth;              // ICVar, 0x190, r_Width
        void *m_pHeight;             // ICVar, 0x198, r_Height
        void *m_pSysSpec;            // ICVar, 0x1A0, sys_spec
        void *m_pSysSpecFull;        // ICVar, 0x1A8, sys_spec_full
        void *m_pWindowType;         // ICVar, 0x1B0, r_WindowType
        void *m_pFps;                // ICVar, 0x1B8, wh_ui_fps
        void *m_pVSync;              // ICVar, 0x1C0, r_VSync
        void *m_pMaxFps;             // ICVar, 0x1C8, sys_MaxFPS
        void *m_pConsoleMode;        // ICVar, 0x1D0, wh_sys_ConsoleMode
        void *m_pSuperResolutionMode; // ICVar, 0x1D8, r_SuperResolution_Mode
        void *m_pDlssPreset;         // ICVar, 0x1E0, r_SuperResolution_NVIDIA_DLSS_Preset
        void *m_pDlssQualityMode;    // ICVar, 0x1E8, r_SuperResolution_NVIDIA_DLSS_QualityMode
        void *m_pFsrQualityMode;     // ICVar, 0x1F0, r_SuperResolution_AMD_FSR_QualityMode
        void *m_pXessQualityMode;    // ICVar, 0x1F8, r_SuperResolution_INTEL_XeSS_QualityMode
        void *m_pSgsrQualityMode;    // ICVar, 0x200, r_SuperResolution_QCOM_SGSR_QualityMode
        void *m_pSuperResolutionSharpness; // ICVar, 0x208, r_SuperResolution_Sharpness
        void *m_pMotionBlur;         // ICVar, 0x210, r_MotionBlur
        void *m_pNearDof;            // ICVar, 0x218, wh_cl_NearDof
        void *m_pHdrOutput;          // ICVar, 0x220, r_HDROutput
        void *m_pHdrUiBrightness;    // ICVar, 0x228, r_HDRUIBrightness
        std::byte m_opaque0230[0xC];       // 0x230; not written by the constructor
        bool m_flag023C;                   // 0x23C; ctor 0
        float m_volumeMaster;              // 0x240; registered with the audio system as "volume_master"
        float m_volumeMusic;               // 0x244; "volume_music"
        float m_volumeSfx;                 // 0x248; "volume_sfx"
        std::byte m_padding024C[0x4];
        void *m_pMasterVolume;       // ICVar, 0x250, s_MasterVolume
        void *m_pMusicVolume;        // ICVar, 0x258, s_MusicVolume
        void *m_pSoundVolume;        // ICVar, 0x260, s_SoundVolume
        void *m_ptrs0268[3];               // 0x268; ctor 0
        std::uint32_t m_resolutionIndex;   // 0x280; sub_180F6D87C(width, height)
        std::byte m_padding0284[0x4];
        std::uint32_t *m_resolutionsBegin; // 0x288; a vector of 4-byte entries
        std::uint32_t *m_resolutionsEnd;   // 0x290
        std::uint32_t *m_resolutionsCapacity; // 0x298
        std::int32_t m_value02A0;          // 0x2A0; ctor sets it to the vector's last index
        std::int32_t m_value02A4;          // 0x2A4; ctor -1
        std::int32_t m_value02A8;          // 0x2A8; ctor -1
        std::byte m_opaque02AC[0xC];       // 0x2AC; not written by the constructor
        std::byte m_specsChangeObserver[0x70]; // 0x2B8; wh::C_ReadinessObserver("SpecsChange")
        float m_value0328;                 // 0x328; ctor -1.0f
        std::byte m_padding032C[0x4];
        void *m_pOwnerMenu;             // 0x330, the C_UIMenu holding this
        std::byte m_keybinds[0x60];        // 0x338; wh::guimodule::C_UIKeybinds (sub_1806003C8)
        std::byte m_photomodeMenu[0xF8];   // 0x398; wh::guimodule::C_UIPhotomodeMenu (sub_181F89F00)

        static void AssertLayout() {
            static_assert(offsetof(C_UISettings, m_pProfileListenerVTable) == 0x058, "C_UISettings profile listener base offset mismatch");
            static_assert(offsetof(C_UISettings, m_pOwnerMenu) == 0x330, "C_UISettings::m_pOwnerMenu offset mismatch");
            static_assert(offsetof(C_UISettings, m_pSubtitlesEnabled) == 0x128, "C_UISettings::m_pSubtitlesEnabled offset mismatch");
            static_assert(offsetof(C_UISettings, m_value0160) == 0x160, "C_UISettings::m_value0160 offset mismatch");
            static_assert(offsetof(C_UISettings, m_pHdrUiBrightness) == 0x228, "C_UISettings::m_pHdrUiBrightness offset mismatch");
            static_assert(offsetof(C_UISettings, m_volumeMaster) == 0x240, "C_UISettings::m_volumeMaster offset mismatch");
            static_assert(offsetof(C_UISettings, m_pMasterVolume) == 0x250, "C_UISettings::m_pMasterVolume offset mismatch");
            static_assert(offsetof(C_UISettings, m_resolutionsBegin) == 0x288, "C_UISettings::m_resolutionsBegin offset mismatch");
            static_assert(offsetof(C_UISettings, m_specsChangeObserver) == 0x2B8, "C_UISettings::m_specsChangeObserver offset mismatch");
            static_assert(offsetof(C_UISettings, m_keybinds) == 0x338, "C_UISettings::m_keybinds offset mismatch");
            static_assert(offsetof(C_UISettings, m_photomodeMenu) == 0x398, "C_UISettings::m_photomodeMenu offset mismatch");
        }
    };

    // C_UIMenu's constructor places it at +0x1D8 and its next own member at
    // +0x668, so on an 8-aligned object the size is exactly the gap.
    static_assert(sizeof(C_UISettings) == 0x490, "C_UISettings size mismatch");
} // namespace SDK::wh::guimodule
