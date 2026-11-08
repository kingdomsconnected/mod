#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../framework/wuid.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::xgenaimodule {
    /**
     * Sends an XGenAI message to the brain `receiver` names, now, the way
     * the game's own C++ does (`SendPlayerSurrenderSignal`,
     * `C_Minigame::SendMessageToInteractionTarget`).
     *
     * `type` is a message type as `Scripts/AI/MessageTypes.xml` lists it.
     * `content` fills its members in the engine's text form: `member(value)`
     * pairs joined by commas -- a WUID as a decimal integer, a vec3 as
     * `(x,y,z)`, a bool as `true` or `false`, as the game's own `hitData`
     * sender writes them (0x1847ACAB0).
     *
     * True when a brain took it into a mailbox whose filter matches the
     * type. False when nothing is registered under the WUID or its brain has
     * no such mailbox: the receiver's slot 19 (`C_NPC`'s is 0x18070DBD0)
     * answers 3. The native ignores RCX -- its callers leave whatever is
     * there -- so the first argument is a placeholder. Its queued twin
     * (0x1804C96F8) is the one that takes the interweb's lock.
     */
    inline bool SendMessageToWuid(const framework::WUID &receiver, const CryStringT<char> &type, const CryStringT<char> &content) {
        return hook::call<bool>(gPatterns.xgenaimodule__SendMessageToWuid, static_cast<void *>(nullptr), &receiver, &type, &content);
    }
} // namespace SDK::wh::xgenaimodule
