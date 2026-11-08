#pragma once

#include <cstdint>

namespace SDK::wh::framework::E_UIApseView {
    /**
     * Which page the pause screen shows, as `I_UIApse::Show` and `SwitchView`
     * take it in one byte. Names from the RTTR registration at 0x180063200.
     *
     * `Inventory` and `Player` frame the character stage; the rest move the
     * camera along the row of panels away from it.
     */
    enum class Type : std::uint8_t {
        None          = 0,
        Crafting      = 1,
        Inventory     = 2,
        InventoryInfo = 3,
        Player        = 4,
        PlayerInfo    = 5,
        Map           = 6,
        Codex         = 7,
        MapLegend     = 8,
        QuestLog      = 9,
        QuestDiary    = 10,
    };
} // namespace SDK::wh::framework::E_UIApseView
