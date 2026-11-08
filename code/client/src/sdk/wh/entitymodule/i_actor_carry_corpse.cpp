#include "i_actor_carry_corpse.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    // `C_ActorCarryCorpse::EndCarrying` is not a slot. The interface is the
    // implementation's primary base, so `this` is the same address either way.
    void I_ActorCarryCorpse::EndCarrying() {
        hook::this_call<void>(gPatterns.C_ActorCarryCorpse__EndCarrying, this);
    }
} // namespace SDK::wh::entitymodule
