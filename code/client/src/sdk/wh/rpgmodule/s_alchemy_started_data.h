#pragma once

#include "../framework/wuid.h"

#include <cstddef>

namespace SDK::wh::rpgmodule {
    /**
     * The payload `C_EventFactory::CreateAlchemyStartedEvent` takes, by its
     * mangled name: just the soul the event is about.
     *
     * Its factory, `C_EventFactory::CreateAlchemyStartedEvent` (0x1809DC2EC),
     * is COMDAT-folded with the other single-WUID causes, and it is the one
     * the Lua `Soul:AddSkillXP` and `Soul:AddStatXP` bindings build their
     * event with before they attach an XP effect to it: the event is only a
     * carrier, and the cause says nothing the XP effect reads.
     */
    struct S_AlchemyStartedData {
        framework::WUID soul;
    };

    static_assert(sizeof(S_AlchemyStartedData) == 0x8, "S_AlchemyStartedData size mismatch");
    static_assert(offsetof(S_AlchemyStartedData, soul) == 0x0, "S_AlchemyStartedData::soul offset mismatch");
} // namespace SDK::wh::rpgmodule
