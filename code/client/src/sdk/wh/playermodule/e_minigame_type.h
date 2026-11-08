#pragma once

#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * `wh::playermodule::E_MinigameType::Type`, the kind every concrete
     * minigame's primary vtable slot 0 returns and every selector of
     * `C_PlayerManager::FindMinigame` (0x182024240) compares against. Only
     * the kinds the mod has proven are named.
     */
    struct E_MinigameType {
        enum Type : std::uint32_t {
            /** `FindMinigame`'s wildcard: the first instance of any kind. */
            Any = 0,

            /** `C_Sharpening`, 0x258 bytes; `C_PlayerManager::CreateMinigame` (0x1809F0234) builds it, and its slot 0 answers 1. */
            Sharpening = 1,

            /** `C_Reading`, 0xE8 bytes; `C_PlayerManager::CreateMinigame` kind 2. */
            Reading = 2,

            /** `C_Alchemy`, 0x6D8 bytes; `C_UIBook::OnMinigameAdded` links it as kind 3. */
            Alchemy = 3,

            /** `C_Dice`, 0x5B8 bytes; `C_ScriptBind_Dice::GetLocalDice` asks for kind 7. */
            Dice = 7,

            /** `C_Pickpocketing`, 0xF8 bytes. */
            Pickpocketing = 8,

            /** `C_StoneThrowing`, 0xC8 bytes; `C_ScriptBind_StoneThrowing::StartMinigame` asks for kind 9. */
            StoneThrowing = 9,

            /** `C_BattleArchery`: the window and battlement lean, and shooting from a battlement. */
            BattleArchery = 10,
            // C_Blacksmithing; factory 0x1809F0234, constructor 0x1809EF36C.
            Blacksmithing = 12,
        };
    };
} // namespace SDK::wh::playermodule
