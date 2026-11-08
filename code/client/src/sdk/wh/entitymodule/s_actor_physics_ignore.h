#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;

    /**
     * One named set of physical entities an actor's collider ignores.
     *
     * The actor constructor builds two inside its physics-ignore block,
     * 0x1811FF97C: kind 0 named "Actor_EntityPhysicsIgnore" at `C_Actor+0x1E8`
     * and kind 1 named "Actor_AdditionalPhysicsIgnore" at `C_Actor+0x208`,
     * each through 0x1811FFA0C.
     */
    struct S_ActorPhysicsIgnore {
        C_Actor *m_pActor; // 0x00
        void *m_pState;    // 0x08, null until the set is first filled
        std::int32_t m_kind; // 0x10
        std::byte m_padding0014[0x4];
        SDK::CryStringT<char> m_name; // 0x18
    };

    static_assert(sizeof(S_ActorPhysicsIgnore) == 0x20, "S_ActorPhysicsIgnore size mismatch");
} // namespace SDK::wh::entitymodule
