#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Where an actor is in the mount/ride/dismount cycle of a cart.
     *
     * Named by the game's RTTR registration of wh::entitymodule::E_CartActorState
     * at 0x180101810. It is stored per actor rather than on C_Cart, in the
     * per-actor cart context the mount code drives through
     * wh::entitymodule::SetCartActorState (0x1828DCA58).
     *
     * The transitions that notify Lua are the ones worth hooking: leaving
     * Mounting posts 'CartNotification:Mounted', and leaving Dismounting or
     * FastDismounting posts 'CartNotification:Dismounted', both on the
     * "carts:codeMessage" channel.
     */
    struct E_CartActorState {
        enum Type : std::uint8_t {
            None            = 0,
            WaitingForMount = 1,
            Mounting        = 2,
            Dismounting     = 3,
            FastDismounting = 4,
            Sitting         = 5,
            RiensGo         = 6,
            RiensStop       = 7,

            Count = 8,
        };
    };

    static_assert(sizeof(E_CartActorState::Type) == 0x1, "E_CartActorState::Type size mismatch");
} // namespace SDK::wh::entitymodule
