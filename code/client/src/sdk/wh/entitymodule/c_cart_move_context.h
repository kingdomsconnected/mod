#pragma once

#include "e_cart_speed.h"
#include "e_cart_state.h"
#include "i_cart.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Where a cart is along its path and where it is going, 48 bytes at
     * C_Cart+0xA0.
     *
     * Made by the setup pass (`CreateCartMoveContext`, 0x180F8F9A4) and
     * integrated once a frame by `UpdateMoveContext` (0x1828D5824), which
     * returns the speed the follower model then advances the cart by. A cart
     * moves only in `Moving` and `Stopping`: `MoveTo` (I_Cart slot 32) sets the
     * target and puts an idle cart into `WaitingForCaravan` or
     * `NeedConfirmation`, and `C_Cart::ConfirmMoveStart` is what lets it go.
     * Within two metres of the target a moving cart starts stopping, and it
     * goes idle below 0.01 m/s.
     *
     * The speeds come from the driver: `SetDriverSpeeds` (0x1828E2234) fills
     * the table from `wh_cart_MovementSpeedNormal` and the driver's own horse
     * gait, and zeroes it with no driver -- so a cart nobody sits at the reins
     * of does not move whatever it is told.
     */
    class C_CartMoveContext {
      public:
        E_CartState::Type GetState() const {
            return m_state;
        }

        E_CartSpeed::Type GetSpeed() const {
            return m_speed;
        }

        float GetTargetDistance() const {
            return m_targetDistance;
        }

        /** The cursor's path distance, restated by every tick before it integrates. */
        float GetCurrentDistance() const {
            return m_currentDistance;
        }

        /**
         * Signed along the path: the follower model advances the cursor by
         * `dt` times this (0x180F8DCAE). Negative only while the travel speed
         * is, which the game itself never makes it -- see `GetGaitSpeed`.
         */
        float GetCurrentSpeed() const {
            return m_currentSpeed;
        }

        /**
         * The travel speed `Update` (0x1828D5824) drives at in `gait`, and the
         * one place the direction of travel comes from: it takes the sign of
         * this entry, never of the target's side (0x1828D5A4C), and caps the
         * result symmetrically. `SetDriverSpeeds` fills both from the driver
         * and only ever with positive values, so natively a cart only drives
         * forward.
         */
        float GetGaitSpeed(E_CartSpeed::Type gait) const {
            return m_speeds[gait];
        }

        void SetGaitSpeed(E_CartSpeed::Type gait, float speed) {
            m_speeds[gait] = speed;
        }

        /**
         * Where the cart is going, in path distance. `Stop` (0x1828DD950)
         * writes it two metres on from where the cart is, in the direction
         * the cart faces along its path (`C_Cart::GetPathDirection`,
         * 0x1828D976C).
         */
        void SetTargetDistance(float distance) {
            m_targetDistance = distance;
        }

      private:
        I_Cart *m_pCart;           // 0x00, the C_Cart it belongs to
        E_CartState::Type m_state; // 0x08
        E_CartSpeed::Type m_speed; // 0x09
        std::byte m_padding000A[0x2];
        float m_speeds[E_CartSpeed::Count];      // 0x0C
        float m_slopeSpeeds[E_CartSpeed::Count]; // 0x14
        float m_targetDistance;                  // 0x1C
        float m_currentDistance;                 // 0x20
        float m_currentSpeed;                    // 0x24
        float m_confirmDelay;                    // 0x28, non-zero sends a start to NeedConfirmation rather than WaitingForCaravan
        float m_smoothingVelocity;               // 0x2C

        static void AssertLayout() {
            static_assert(offsetof(C_CartMoveContext, m_state) == 0x08, "C_CartMoveContext::m_state offset mismatch");
            static_assert(offsetof(C_CartMoveContext, m_speed) == 0x09, "C_CartMoveContext::m_speed offset mismatch");
            static_assert(offsetof(C_CartMoveContext, m_speeds) == 0x0C, "C_CartMoveContext::m_speeds offset mismatch");
            static_assert(offsetof(C_CartMoveContext, m_slopeSpeeds) == 0x14, "C_CartMoveContext::m_slopeSpeeds offset mismatch");
            static_assert(offsetof(C_CartMoveContext, m_targetDistance) == 0x1C, "C_CartMoveContext::m_targetDistance offset mismatch");
            static_assert(offsetof(C_CartMoveContext, m_currentDistance) == 0x20, "C_CartMoveContext::m_currentDistance offset mismatch");
            static_assert(offsetof(C_CartMoveContext, m_currentSpeed) == 0x24, "C_CartMoveContext::m_currentSpeed offset mismatch");
            static_assert(offsetof(C_CartMoveContext, m_confirmDelay) == 0x28, "C_CartMoveContext::m_confirmDelay offset mismatch");
        }
    };

    static_assert(sizeof(C_CartMoveContext) == 0x30, "C_CartMoveContext size mismatch");
} // namespace SDK::wh::entitymodule
