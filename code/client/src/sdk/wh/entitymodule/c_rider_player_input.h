#pragma once

#include "../../cryengine/system/cry_name.h"
#include "../../patterns.h"
#include "../../types.h"
#include "c_horse.h"
#include "i_horse_move_adapter.h"
#include "i_movement_input_listener.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Player;

    struct RiderInputBindingTreeLayout {
        void *m_pHead;
        std::uint64_t m_size;
    };

    static_assert(sizeof(RiderInputBindingTreeLayout) == 0x10,
                  "RiderInputBindingTreeLayout size mismatch");

    /** Large inline action state; construction stores the owning player at +0x8B8. */
    struct RiderInputActionStateLayout {
        // Built by 0x180A4D24C (at C_RiderPlayerInput+0x30).
        std::byte m_opaque0000[0x868];   // 0x000, built by 0x180A4D370
        std::byte m_state0868[0x48];     // 0x868, built by 0x1809CDEA4 and given a std::function by 0x18070D458
        float m_value08B0;               // 0x8B0, set to 0.3
        std::byte m_padding08B4[0x4];
        C_Player *m_pPlayer;             // 0x8B8
        std::uint32_t m_value08C0;       // 0x8C0, zeroed
        float m_value08C4;               // 0x8C4, set to 0.2
        float m_value08C8;               // 0x8C8, set to 0.4
        std::uint32_t m_value08CC;       // 0x8CC, zeroed
        std::uint64_t m_value08D0;       // 0x8D0, zeroed
        std::uint32_t m_value08D8;       // 0x8D8, zeroed
        float m_values08DC[4][4];        // 0x8DC, each row set to {1, 0, 0, 0}
        std::uint32_t m_values091C[3];   // 0x91C, zeroed
        std::uint16_t m_value0928;       // 0x928, zeroed
        std::uint8_t m_value092A;        // 0x92A, zeroed
        std::byte m_padding092B[0x5];

      private:
        static void AssertLayout() {
            static_assert(offsetof(RiderInputActionStateLayout, m_value08B0) == 0x8B0, "RiderInputActionStateLayout::m_value08B0 offset mismatch");
            static_assert(offsetof(RiderInputActionStateLayout, m_pPlayer) == 0x8B8, "RiderInputActionStateLayout::m_pPlayer offset mismatch");
            static_assert(offsetof(RiderInputActionStateLayout, m_values08DC) == 0x8DC, "RiderInputActionStateLayout::m_values08DC offset mismatch");
            static_assert(offsetof(RiderInputActionStateLayout, m_value092A) == 0x92A, "RiderInputActionStateLayout::m_value092A offset mismatch");
        }
    };

    static_assert(sizeof(RiderInputActionStateLayout) == 0x930,
                  "RiderInputActionStateLayout size mismatch");

    struct RiderInputCallbackLayout {
        void *m_pObject;
        void *m_pInvoke;
    };

    static_assert(sizeof(RiderInputCallbackLayout) == 0x10,
                  "RiderInputCallbackLayout size mismatch");

    /**
     * Concrete adapter translating rider/player input into horse movement.
     *
     * RTTI proves I_HorseMoveAdapter at +0x00 and
     * I_MovementInputListener at +0x08. Its primary and secondary tables
     * implement exactly the two base contracts and add no operations. The
     * creation path allocates exactly 0xA18 bytes.
     */
    class C_RiderPlayerInput : public I_HorseMoveAdapter,
                               public I_MovementInputListener {
      public:
        ~C_RiderPlayerInput() override = default;

        /** CryEngine's EActionActivationMode values the rider's handlers test. */
        enum E_ActivationMode : int {
            ActivationPress   = 1,
            ActivationRelease = 2,
            ActivationHold    = 4,
        };

        C_Player *GetPlayer() const { return m_pPlayer; }

        /** The horse `SetHorse` (0x1810EA730) bound this input to, or null. */
        C_Horse *GetHorse() const {
            return m_pHorse;
        }

        /**
         * Offers an action to the rider's own handlers (0x1808E5688), as
         * `C_PlayerInput::OnAction` does first for every action it is sent:
         * true when one of them took it, and false without a horse bound.
         *
         * The constructor (0x180A4D57C) binds, from `C_EntityActions`:
         * `sprint` to `OnSprint` (0x180EFE074) -- hold sets `IsSprintHeld`, a
         * press with a value clears the walk toggle and, inside the
         * EntityModule cvar's double-press window (+0xC8) of the previous
         * press, asks for a spur; `toggle_run` to `OnToggleRun`
         * (0x1829F210C), which flips `IsWalkToggled` on release;
         * `horse_slowdown` to `OnSlowdown` (0x181A88670); and the two look
         * axes. `BuildHorseMovementRequest` (0x180A4F304) turns the latches
         * into the request's gait flags: walk 0x02, sprint 0x10, spur 0x20,
         * slowdown 0x80.
         */
        bool HandleAction(const SDK::CCryName &action, E_ActivationMode activationMode, float value) {
            return hook::this_call<bool>(SDK::gPatterns.C_RiderPlayerInput__HandleAction, this, &action, static_cast<int>(activationMode), value);
        }

        /**
         * The `horse_slowdown` handler (0x181A88670), bound by the constructor
         * to an action it interns itself, so `C_EntityActions` has no id for
         * it. The action is press-only (`defaultProfile.xml`), and the
         * handler reads none of its arguments -- one store of the slowdown
         * latch and `return false` -- so it is called with the receiver alone.
         */
        void OnSlowdown() {
            hook::this_call<bool>(SDK::gPatterns.C_RiderPlayerInput__OnSlowdown, this);
        }

        bool IsWalkToggled() const {
            return m_walkToggled != 0;
        }
        bool IsSprintHeld() const {
            return m_sprintHeld != 0;
        }

        /** Game time of the last `sprint` press with a value; the spur window is measured from it. */
        float GetLastSprintPressTime() const {
            return m_lastSprintPressTime;
        }
        RiderInputActionStateLayout *GetActionState() { return &m_actionState; }
        const RiderInputActionStateLayout *GetActionState() const { return &m_actionState; }

      private:
        C_Player *m_pPlayer; // 0x10
        C_Horse *m_pHorse;   // 0x18, written only by SetHorse (0x1810EA730)
        RiderInputBindingTreeLayout m_bindingTree; // 0x20
        RiderInputActionStateLayout m_actionState; // 0x30
        std::byte m_mountedMotionState[0x40]; // 0x960
        std::uint8_t m_frameReady;            // 0x9A0, set by OnMovementInput, consumed by Update (0x180A4EAB4)
        std::byte m_padding09A1[0x7];
        void **m_ppValuesBegin;    // 0x9A8
        void **m_ppValuesEnd;      // 0x9B0
        void **m_ppValuesCapacity; // 0x9B8
        std::uint32_t m_state09C0; // 0x9C0, OnMovementInput: the request's value at +0x38 while it asks for a sprint
        std::uint8_t m_requestSprint;     // 0x9C4, OnMovementInput: request flag 0x4
        std::uint8_t m_requestJump;       // 0x9C5, OnMovementInput: request flag 0x80000
        std::uint8_t m_walkToggled;       // 0x9C6, OnToggleRun
        std::uint8_t m_sprintHeld;        // 0x9C7, OnSprint
        std::uint8_t m_slowdownRequested; // 0x9C8, OnSlowdown, consumed by BuildHorseMovementRequest
        std::uint8_t m_spurRequested;     // 0x9C9, OnSprint, consumed by BuildHorseMovementRequest
        std::byte m_padding09CA[0x2];
        SDK::Vec3 m_lookInput;  // 0x9CC, OnMovementInput's third argument
        SDK::Vec3 m_stickInput; // 0x9D8, OnMovementInput: the owning C_PlayerInput's move vector
        std::uint16_t m_state09E4;
        std::byte m_padding09E6[0x2];
        std::uint32_t m_state09E8;
        float m_state09EC; // initialized to maximum finite float
        float m_lastSprintPressTime; // 0x9F0, OnSprint
        std::uint32_t m_state09F4;
        RiderInputCallbackLayout m_callback; // 0x9F8
        std::uint64_t m_state0A08;
        std::uint16_t m_state0A10;
        std::byte m_padding0A12[0x6];

        static void AssertLayout() {
            static_assert(offsetof(C_RiderPlayerInput, m_pPlayer) == 0x10, "C_RiderPlayerInput::m_pPlayer offset mismatch");
            static_assert(offsetof(C_RiderPlayerInput, m_pHorse) == 0x18, "C_RiderPlayerInput::m_pHorse offset mismatch");
            static_assert(offsetof(C_RiderPlayerInput, m_frameReady) == 0x9A0, "C_RiderPlayerInput::m_frameReady offset mismatch");
            static_assert(offsetof(C_RiderPlayerInput, m_requestSprint) == 0x9C4, "C_RiderPlayerInput::m_requestSprint offset mismatch");
            static_assert(offsetof(C_RiderPlayerInput, m_walkToggled) == 0x9C6, "C_RiderPlayerInput::m_walkToggled offset mismatch");
            static_assert(offsetof(C_RiderPlayerInput, m_sprintHeld) == 0x9C7, "C_RiderPlayerInput::m_sprintHeld offset mismatch");
            static_assert(offsetof(C_RiderPlayerInput, m_spurRequested) == 0x9C9, "C_RiderPlayerInput::m_spurRequested offset mismatch");
            static_assert(offsetof(C_RiderPlayerInput, m_lookInput) == 0x9CC, "C_RiderPlayerInput::m_lookInput offset mismatch");
            static_assert(offsetof(C_RiderPlayerInput, m_stickInput) == 0x9D8, "C_RiderPlayerInput::m_stickInput offset mismatch");
            static_assert(offsetof(C_RiderPlayerInput, m_lastSprintPressTime) == 0x9F0, "C_RiderPlayerInput::m_lastSprintPressTime offset mismatch");
        }
    };

    static_assert(sizeof(C_RiderPlayerInput) == 0xA18,
                  "C_RiderPlayerInput size mismatch");
} // namespace SDK::wh::entitymodule
