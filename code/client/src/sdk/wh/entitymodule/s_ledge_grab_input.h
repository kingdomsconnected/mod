#pragma once

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * The input a player's vault was asked with, the block
     * `C_PlayerInput::PreUpdate` builds through 0x1808301F0 from its action
     * request flags (+0x18) and hands to `TryLedgeGrab` as its last argument.
     *
     * For a player-controlled actor the ledge gate 0x181533840 refuses the
     * vault when the block is missing, and otherwise requires the jump byte --
     * or, for a type-9 ledge, the crouch byte. An AI's vault comes through
     * `C_ActorStateMovement::Ground` with no block and skips that gate.
     */
    struct S_LedgeGrabInput {
        bool jumpRequested;   // 0x00, request bit 0, set by the jump key
        bool crouchRequested; // 0x01, request bit 1, set by OnToggleCrouch

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_LedgeGrabInput, crouchRequested) == 0x01, "S_LedgeGrabInput::crouchRequested offset mismatch");
        }
    };

    static_assert(sizeof(S_LedgeGrabInput) == 0x2, "S_LedgeGrabInput size mismatch");
} // namespace SDK::wh::entitymodule
