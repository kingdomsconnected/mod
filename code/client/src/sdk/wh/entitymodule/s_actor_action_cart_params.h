#pragma once

#include "../../types.h"
#include "c_cart_player_control.h"
#include "e_cart_position.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What `C_ActorStateExpansion::RequestCartAction` (slot 17, 0x18289FAB8)
     * puts a body in a cart with, 16 bytes; copied whole into the
     * `C_ActorActionCart` it builds, at +0x50.
     *
     * `C_CartPlayerControl::MountPlayer` fills it for the local player -- the
     * co-driver's bench or the right rail, the approach walked, its own control
     * -- and the game's NPC seat operation fills it for everyone else, with no
     * control at all. Every read of the control is null-guarded, which is what
     * lets a body without one be seated the same way.
     */
    struct S_ActorActionCartParams {
        EntityId cartEntityId;                  // 0x00, the WHCart entity
        E_CartPosition::Type position;          // 0x04
        bool skipApproach;                      // 0x05, straight to the seat: C_ActorActionCart::Enter then also takes a cart that is moving
        std::byte m_padding0006[0x2];
        C_CartPlayerControl *pPlayerControl;    // 0x08, the local player's, or null

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_ActorActionCartParams, position) == 0x04, "S_ActorActionCartParams::position offset mismatch");
            static_assert(offsetof(S_ActorActionCartParams, skipApproach) == 0x05, "S_ActorActionCartParams::skipApproach offset mismatch");
            static_assert(offsetof(S_ActorActionCartParams, pPlayerControl) == 0x08, "S_ActorActionCartParams::pPlayerControl offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorActionCartParams) == 0x10, "S_ActorActionCartParams size mismatch");
} // namespace SDK::wh::entitymodule
