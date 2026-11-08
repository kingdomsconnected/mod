#pragma once

#include "../../types.h"
#include "../framework/wuid.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What a carry-item action is built from: the request
     * `C_ActionActor::RequestCarryItem` (0x181391738) takes, and the
     * parameters the action holds at +0x50 -- RTTI names the action
     * `C_ParamAction<S_CarryItemActionParams, ...>`.
     *
     * Both of the game's callers fill it: `C_CarryItemPile::PickUpImpl`
     * (0x18139146C) with its pile's animation tag and pack flag, and
     * `C_ScriptBindCarryableItem::RequestPickUp` (0x182AC0568) with an empty
     * tag and no pack -- the carryable quest sacks. The tag is copied through
     * the engine's own `operator=`, so it has to be a string the engine built.
     */
    struct S_CarryItemActionParams {
        framework::WUID item;           // 0x00
        CryStringT<char> animationTag;  // 0x08, an extra pick-up tag; empty for none
        bool pack;                      // 0x10, the pile's pack-and-pick variant
        std::byte m_pad0011[0x7];
    };

    static_assert(offsetof(S_CarryItemActionParams, animationTag) == 0x08, "S_CarryItemActionParams::animationTag offset mismatch");
    static_assert(offsetof(S_CarryItemActionParams, pack) == 0x10, "S_CarryItemActionParams::pack offset mismatch");
    static_assert(sizeof(S_CarryItemActionParams) == 0x18, "S_CarryItemActionParams size mismatch");
} // namespace SDK::wh::entitymodule
