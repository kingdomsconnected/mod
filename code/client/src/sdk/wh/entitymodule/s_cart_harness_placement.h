#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * Where one hitched horse is to be this tick, as `UpdateCartFollowers`
     * (0x180F8DB48) hands it to `DriveHorseToHarness` (0x1828E15F8), built on
     * its stack per harness slot.
     *
     * `DriveHorseToHarness` walks the horse there with a movement request
     * that keeps the harness heading while the horse is within 0.75 m of it,
     * and otherwise -- or whenever `placeOutright` is set -- sets the horse's
     * world transform to it outright. With the cart still (`motion` zero and
     * `placeOutright` clear) it only draws the reins.
     */
    struct S_CartHarnessPlacement {
        Quat rotation;           // 0x00, the harness helper's world rotation
        Vec3 position;           // 0x10, its world position
        EntityId driverEntityId; // 0x1C, who holds the reins, for the reins modifier
        float motion;            // 0x20, zero while the cart does not move
        bool placeOutright;      // 0x24
        std::byte padding0025[0x3];

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_CartHarnessPlacement, position) == 0x10, "S_CartHarnessPlacement::position offset mismatch");
            static_assert(offsetof(S_CartHarnessPlacement, driverEntityId) == 0x1C, "S_CartHarnessPlacement::driverEntityId offset mismatch");
            static_assert(offsetof(S_CartHarnessPlacement, motion) == 0x20, "S_CartHarnessPlacement::motion offset mismatch");
            static_assert(offsetof(S_CartHarnessPlacement, placeOutright) == 0x24, "S_CartHarnessPlacement::placeOutright offset mismatch");
        }
    };

    static_assert(sizeof(S_CartHarnessPlacement) == 0x28, "S_CartHarnessPlacement size mismatch");
} // namespace SDK::wh::entitymodule
