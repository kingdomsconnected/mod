#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What a body is standing, sitting or lying in, as its model records it.
     *
     * Read straight off the game's own name table, which
     * `InitStanceCategoryNames180251FE0` fills and RTTR registers under
     * `wh::entitymodule::E_StanceCategory`. It lives on the actor model at
     * `+0x80` (`C_ActorModel::GetStanceCategory`) and is written by
     * `C_ActorModel::SetStance`.
     *
     * Only `Lying` and `Sitting` select an idle fragment of their own: the
     * actor's idle-state object plays `FragmentIdleStateDatabase`'s
     * `lying` and `SittingIdle` for them and nothing for the rest.
     */
    struct E_StanceCategory {
        enum Type : std::uint32_t {
            Undefined = 0,
            Standing  = 1,
            Lying     = 2,
            Sitting   = 3,
            Kneel     = 4,
            Horse     = 5,
            Crouch    = 6,
            Cart      = 7,
        };
    };
} // namespace SDK::wh::entitymodule
