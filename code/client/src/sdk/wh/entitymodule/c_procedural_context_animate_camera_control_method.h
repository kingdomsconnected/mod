#pragma once

#include "../../patterns.h"
#include "c_actor.h"

#include <utils/hooking/hooking.h>

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What the `AnimateCamera` clip (`wh::game::C_ProceduralClipAnimateCamera`,
     * vtable 0x183B48B50) asks an action controller for by the name
     * `ProceduralContextACCM`: the requests that hold the global
     * `C_CameraManager`'s first-person camera on the animation, so the view
     * follows the head through a climb. The player's own climbs onto a cart
     * carry the clip; the NPC climbs do not.
     */
    class C_ProceduralContextAnimateCameraControlMethod final {
      public:
        /** The clip's `OnEnter` (0x1812EC87C) request. Answers the request's id. */
        std::uint32_t RequestAnimatedCamera(float offset, float blendPosition, float blendTime) {
            return hook::this_call<std::uint32_t>(gPatterns.C_ProceduralContextAnimateCameraControlMethod__Request, this, offset, blendPosition, blendTime);
        }

        /** The clip's `OnExit` (0x1812EC8D8) release: the last one gives the view back to `actor`. */
        void ReleaseAnimatedCamera(std::uint32_t request, float blendTime, C_Actor *actor) {
            hook::this_call<void>(gPatterns.C_ProceduralContextAnimateCameraControlMethod__Release, this, request, blendTime, actor);
        }
    };
} // namespace SDK::wh::entitymodule
