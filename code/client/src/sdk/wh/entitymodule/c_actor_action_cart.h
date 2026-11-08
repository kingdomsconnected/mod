#pragma once

#include "../../types.h"
#include "c_action_actor.h"
#include "e_cart_actor_state.h"
#include "i_cart_driver.h"
#include "s_actor_action_cart_params.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * A body's stay in a cart, from the walk to the seat to the climb down:
     * 0xA0 bytes from the factory at 0x1828BC6C8, four vtables, queued by
     * `C_ActorStateExpansion::RequestCartAction`.
     *
     * Only what `SetCartActorState` (0x1828DCA58) reads is named. That free
     * function is the one place the stay moves from one `E_CartActorState` to
     * the next -- leaving `Mounting` is the seat taken, `None` from the
     * action's `Exit` is the seat given back -- so the members it touches are
     * the ones a hook there needs.
     */
    class C_ActorActionCart {
      public:
        EntityId GetCartEntityId() const {
            return m_params.cartEntityId;
        }

        E_CartPosition::Type GetPosition() const {
            return m_params.position;
        }

        C_ActionActor *GetActionActor() const {
            return m_pActionActor;
        }

        E_CartActorState::Type GetState() const {
            return m_state;
        }

        /**
         * The stay as the holder of its cart's reins: the `I_CartDriver`
         * subobject at +0x78, which `OnMounted` hands to `C_Cart::SetDriver`
         * for the driver's and the teamster's seats.
         */
        I_CartDriver *GetCartDriver() {
            return reinterpret_cast<I_CartDriver *>(&m_cartDriver);
        }

        /**
         * Gets the body out the way the dismount key does: the climb down, or
         * with `fast` the quick drop. Deferred to the action's own update while
         * the body is not visible. Slot 37 of the primary table, called by
         * address rather than declared: the 36 slots before it are not
         * modeled.
         */
        void RequestDismount(bool fast) {
            hook::this_call<void>(gPatterns.C_ActorActionCart__RequestDismount, this, fast);
        }

      private:
        std::byte m_opaque0000[0x50];
        S_ActorActionCartParams m_params; // 0x50
        std::byte m_actorObject0060[0x8]; // 0x60, C_ActorObject vtable (base ctor 0x1828D3378)
        C_ActionActor *m_pActionActor; // 0x68, the body's action actor; its C_Actor is at +0x28
        std::uint16_t m_state0070;     // 0x70, base ctor zeroes
        std::byte m_padding0072[0x2];
        std::uint32_t m_state0074;     // 0x74, base ctor zeroes
        void *m_cartDriver;                     // 0x78, the I_CartDriver subobject's table (0x18470E7D8)
        E_CartActorState::Type m_state;         // 0x80
        E_CartActorState::Type m_deferredState; // 0x81, consumed by Update out of Sitting
        std::byte m_padding0082[0x6];
        std::int64_t m_time0088;                // 0x88, factory 0x1828BC6C8 seeds -100000 (a "never" timestamp)
        void *m_value0090;                      // 0x90, factory zeroes
        void *m_value0098;                      // 0x98, factory zeroes

        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionCart, m_params) == 0x50, "C_ActorActionCart::m_params offset mismatch");
            static_assert(offsetof(C_ActorActionCart, m_pActionActor) == 0x68, "C_ActorActionCart::m_pActionActor offset mismatch");
            static_assert(offsetof(C_ActorActionCart, m_cartDriver) == 0x78, "C_ActorActionCart::m_cartDriver offset mismatch");
            static_assert(offsetof(C_ActorActionCart, m_state) == 0x80, "C_ActorActionCart::m_state offset mismatch");
            static_assert(offsetof(C_ActorActionCart, m_deferredState) == 0x81, "C_ActorActionCart::m_deferredState offset mismatch");
            static_assert(offsetof(C_ActorActionCart, m_state0074) == 0x74, "C_ActorActionCart::m_state0074 offset mismatch");
            static_assert(offsetof(C_ActorActionCart, m_time0088) == 0x88, "C_ActorActionCart::m_time0088 offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorActionCart) == 0xA0, "C_ActorActionCart size mismatch");
} // namespace SDK::wh::entitymodule
