#pragma once

#include "../../types.h"

namespace SDK::wh::entitymodule {
    /**
     * Whatever holds a cart's reins: what `C_Cart::SetDriver` (0x1828DC678)
     * stores at `C_Cart+0x90`, and what the cart's movement asks before it
     * moves at all -- `BeginMove` (0x1828DB1E8) refuses unless slot 0
     * answers true (0x180F8ECCC), and `SetDriver` refills the cart's speeds
     * from the actor slot 1 names (`SetDriverSpeeds`, 0x1828E2234).
     *
     * Implemented by `C_ActorActionCart` as the subobject at +0x78 (vtable
     * 0x18470E7D8). `C_ActorActionCart::OnMounted` (0x1828DAF80) registers a
     * body's stay as the driver when its seat is the driver's or the
     * teamster's, and `Exit` releases it for those seats through the helper
     * at 0x1828DF194, under the engine lock.
     */
    class I_CartDriver {
      public:
        /** Sitting, flicking the reins or pulling them (`E_CartActorState` 5 to 7). 0x1828DA820. */
        virtual bool IsSeated() const = 0; // slot 0

        /** The entity id of the body holding the reins. 0x181A83E30. */
        virtual EntityId GetDriverEntityId() const = 0; // slot 1

        /**
         * The cart asks to set off (`C_Cart::NotifyDriverMoveStart`): queues
         * the reins flick and arms the 0.7-1.4 s timer after which the stay's
         * update confirms the start. 0x1828DB234, takes the `C_Cart`.
         */
        virtual void OnCartMoveStart(void *cart) = 0; // slot 2

        /** The cart stops: queues the reins pull. 0x1828D4CD4. */
        virtual void OnCartMoveStop() = 0; // slot 3

        /** Releases the reins (0x1828DF194, driver's and teamster's seats only) and ends the cart action itself. 0x1828DB790. */
        virtual void ReleaseReins() = 0; // slot 4
    };
} // namespace SDK::wh::entitymodule
