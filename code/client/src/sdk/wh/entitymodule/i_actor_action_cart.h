#pragma once

#include "i_actor_action.h"

namespace SDK::wh::entitymodule {
    /**
     * The interface of the cart stay action. Its RTTI hierarchy descriptor
     * (0x18429DB18) lists I_ActorAction, I_Action and the reference-counted
     * base beneath it, and C_ActorActionCart's framework base is
     * `C_Action<I_ActorActionCart>` (base ctor 0x1828D3378). The slots it
     * adds are not modeled.
     */
    class I_ActorActionCart: public I_ActorAction {};

    static_assert(sizeof(I_ActorActionCart) == 0x10, "I_ActorActionCart size mismatch");
} // namespace SDK::wh::entitymodule
