#pragma once

#include <cstdint>
#include <iterator>
#include <string_view>

namespace SDK::wh::framework::E_HudElements {
    /**
     * One element of the in-game HUD, as `C_UIHudMask` masks it.
     *
     * Names from 0x180555AC0; the native std::function RTTI at 0x183EEB1F8
     * establishes wh::framework::E_HudElements::Type. The mask passes the
     * one-byte value in DL to its visibility query.
     */
    enum class Type : std::uint8_t {
        Compass          = 0,
        Stats            = 1, // the health, stamina and nourishment bars along the bottom
        QAMWeapon        = 2,
        QAMFood          = 3,
        Subtitles        = 4,
        InfoText         = 5,
        GameLog          = 6,
        Hints            = 7,
        DialogLeft       = 8,
        DialogRight      = 9,
        Cursor           = 10,
        Crime            = 11,
        Wanted           = 12,
        PopUpBackground  = 13,
        TutorialMessage  = 14,
        FancyEvent       = 15,
        SkillCheck       = 16,
        ItemTransfer     = 17,
        Buffs            = 18,
        CommonEvent      = 19,
        DiceCursor       = 20,
        Trespassing      = 21,
        RatioStrips      = 22,
        ShootingContest  = 23,
        Bubbles          = 24,
        TutorialInDialog = 25,
        DiceContainer    = 26,
        Vignette         = 27,
    };

    /** Indexed by the enum value. */
    inline constexpr std::string_view kNames[] = {
        "Compass",
        "Stats",
        "QAMWeapon",
        "QAMFood",
        "Subtitles",
        "InfoText",
        "GameLog",
        "Hints",
        "DialogLeft",
        "DialogRight",
        "Cursor",
        "Crime",
        "Wanted",
        "PopUpBackground",
        "TutorialMessage",
        "FancyEvent",
        "SkillCheck",
        "ItemTransfer",
        "Buffs",
        "CommonEvent",
        "DiceCursor",
        "Trespassing",
        "RatioStrips",
        "ShootingContest",
        "Bubbles",
        "TutorialInDialog",
        "DiceContainer",
        "Vignette",
    };

    inline constexpr std::string_view ToString(Type element) {
        return kNames[static_cast<std::uint8_t>(element)];
    }

    /** The element spelled this way, or nothing. Compared exactly, as the console variable name is. */
    inline bool FromString(std::string_view name, Type &element) {
        for (std::uint8_t index = 0; index < std::size(kNames); ++index) {
            if (kNames[index] == name) {
                element = static_cast<Type>(index);
                return true;
            }
        }
        return false;
    }
    static_assert(sizeof(Type) == 1);
} // namespace SDK::wh::framework::E_HudElements
