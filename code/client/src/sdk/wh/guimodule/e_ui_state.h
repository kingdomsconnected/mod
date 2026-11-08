#pragma once

#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * What the player is doing, and which HUD elements are suppressed --
     * C_UIStateBlockers holds both. Transcribed from the value-to-name switch
     * at 0x180BC621C, which covers 0 through 50 with no gaps.
     */
    enum class E_UIState : std::uint8_t {
        Allowed                     = 0,
        Fader                       = 1,
        Dialog                      = 2,
        Chat                        = 3,
        Cutscene                    = 4,
        CutsceneCapture             = 5,
        TextCutscene                = 6,
        ScriptTutorialBlock         = 7,
        GameOver                    = 8,
        Menu                        = 9,
        LoadScreen                  = 10,
        Apse                        = 11,
        Shop                        = 12,
        ItemSelection               = 13,
        ItemTransfer                = 14,
        FastTravel                  = 15,
        SkipTimeDialog              = 16,
        SkipTimeRunning             = 17,
        MinigameAlchemy             = 18,
        MinigameHerbGathering       = 19,
        MinigameHoleDigging         = 20,
        MinigamePickpocketing       = 21,
        MinigamePickpocketingCharge = 22,
        MinigameReading             = 23,
        MinigameLockPicking         = 24,
        MinigameSharpening          = 25,
        MinigameDice                = 26,
        MinigameArchery             = 27,
        MinigameBlacksmithing       = 28,
        MinigameStoneThrowing       = 29,
        MinigameBattleArchery       = 30,
        HelpOverlay                 = 31,
        HorseInspect                = 32,
        CrimeList                   = 33,
        HitReaction                 = 34,
        BlockCommonNotifications    = 35,
        Photomode                   = 36,
        CameraWithoutOverlay        = 37,
        FirstTimeOverlays           = 38,
        BlockCompass                = 39,
        BlockCrime                  = 40,
        BlockStaminaBar             = 41,
        BlockBuffs                  = 42,
        BlockCursor                 = 43,
        BlockLevelUp                = 44,
        BlockFlushNotifications     = 45,
        Saving                      = 46,
        DialogBarber                = 47,
        DialogShieldPainting        = 48,
        MinigameForgeBuilder        = 49,
        Distract                    = 50,
    };
} // namespace SDK::wh::guimodule
