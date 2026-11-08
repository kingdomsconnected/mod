#pragma once

#include "../../cryengine/action/i_game_object.h"
#include "../../cryengine/system/i_system_event_listener.h"
#include "../../patterns.h"
#include "../../types.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Player;

    /** MSVC std::set<ActionId> storage used to track currently held actions. */
    struct PlayerInputActiveActionSetLayout {
        void *m_pHead;
        std::uint64_t m_size;
    };

    static_assert(sizeof(PlayerInputActiveActionSetLayout) == 0x10, "PlayerInputActiveActionSetLayout size mismatch");

    /**
     * Local-player action and system-event input handler.
     *
     * RTTI proves IActionListener at +0x00 and ISystemEventListener at +0x08.
     * The corresponding tables at 0x183EC99A8 and 0x183EC99C8 contain exactly
     * the inherited contracts and add no virtual operations. Construction
     * stores C_Player at +0x10, and scalar deletion proves size 0x2D8. The
     * concrete OnAction override maintains an MSVC std::set<ActionId> at
     * +0x20: press inserts, release erases, and system event 55 drains the
     * set by synthesizing releases for every remaining action.
     *
     * The constructor at 0x180799E9C also binds one handler per movement
     * action, taking the ActionIds from the single C_EntityActions at
     * C_WHGameModules+0x30. Those handlers are the only writers of the
     * fields named below, which is what proves them.
     */
    class C_PlayerInput
        : public SDK::IActionListener
        , public SDK::ISystemEventListener {
      public:
        /** Bits OR'd into the action request flags by the bound handlers. */
        enum E_ActionRequest : std::uint32_t {
            RequestJumpMask   = 0x1,
            RequestCrouchMask = 0x2,
            RequestSprintMask = 0x4,
            RequestMovingMask = 0x10,
        };

        ~C_PlayerInput() override = default;

        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;

        C_Player *GetPlayer() const {
            return m_pPlayer;
        }
        std::uint64_t GetActiveActionCount() const {
            return m_activeActions.m_size;
        }

        /**
         * The walk/jog mode "toggle_run" switches between; 1 is jog.
         *
         * The constructor stores 1 at 0x180799F18, which is the whole reason
         * the game starts in jog: no cvar, no profile entry, no serialisation.
         * OnToggleRun flips it on key release, and OnSprint forces it back to
         * 1 on every frame the sprint key is held -- the action is declared
         * `onHold="1"` with the engine's default zero repeat delay.
         *
         * `ApplyMoveStyleToRequest` (0x1808307D8) is the only reader, and it
         * applies walk as a floor on this frame's movement request rather than
         * as a stored speed, so a walk written during a sprint clamps the
         * sprint. It reads m_analogMovement first: the mode governs digital
         * movement only.
         */
        bool IsRunMode() const {
            return m_runMode != 0;
        }
        void SetRunMode(bool run) {
            m_runMode = static_cast<std::uint8_t>(run ? 1 : 0);
        }

        /**
         * Whether the body is sprinting, rather than whether the key is down.
         *
         * `C_PlayerInput::PreUpdate` (0x18082F77C) owns the sprint through
         * this bit, calling `StartSprint` (0x18148291C) and `StopSprint`
         * (0x181482A08) -- the latter when movement stops, when stamina runs
         * out, or when `OnSprint` ends a toggled sprint. The held byte at
         * C_Player+0xB20 is only the key, and `cl_sprintToggle` leaves it
         * clear for most of a sprint.
         */
        bool IsSprinting() const {
            return (m_actionRequestFlags & RequestSprintMask) != 0;
        }

        /**
         * Leaves the move axes as the digital move handlers do for held keys.
         *
         * `OnMoveForward`/`Back`/`Left`/`Right` (0x181313408 and its
         * siblings, through 0x181313444) write each axis twice -- the move
         * vector `PreUpdate` consumes and the raw axis beside it -- and set or
         * clear the moving bit. A stick fed through `OnXiMoveX`/`Y` before is
         * dropped, as `PreUpdate` would otherwise copy its axis over these.
         */
        void SetMovementInput(const SDK::Vec3 &direction) {
            m_movementInput    = direction;
            m_rawMovementInput = SDK::Vec2 {direction.x, direction.y};
            m_analogMovement   = 0;
            if (direction.x * direction.x + direction.y * direction.y + direction.z * direction.z > 0.0001F) {
                m_actionRequestFlags |= RequestMovingMask;
            }
            else {
                m_actionRequestFlags &= ~RequestMovingMask;
            }
        }

        /**
         * Asks for a jump the way the bound jump handler does. `PreUpdate`
         * turns the bit into the movement request's jump flag (0x80000) and
         * clears it, and the movement HSM's ground pass decides from there.
         */
        void RequestJump() {
            m_actionRequestFlags |= RequestJumpMask;
        }

        /**
         * Asks for a crouch toggle the way `OnToggleCrouch` (0x1819E0430)
         * does on press. `ApplyMoveStyleToRequest` (0x1808307D8), inside
         * `PreUpdate`, answers it with the state expansion's
         * `SetCrouchRequested(!IsCrouchRequested(), false)` and clears it.
         */
        void RequestCrouchToggle() {
            m_actionRequestFlags |= RequestCrouchMask;
        }

        void StopSprint() {
            hook::this_call<void>(SDK::gPatterns.C_PlayerInput__StopSprint, this);
        }

        void StartSprint() {
            hook::this_call<void>(SDK::gPatterns.C_PlayerInput__StartSprint, this);
        }

        void SetSprintRequested(bool requested) {
            hook::this_call<void>(SDK::gPatterns.C_PlayerInput__SetSprintRequested, this, requested);
        }

        /**
         * The movement stick's handlers, `OnXiMoveX` (0x180EFE1E8) and
         * `OnXiMoveY` (0x180EFE20C), bound to `C_EntityActions::GetXiMoveX`
         * and `GetXiMoveY`.
         *
         * Both go through 0x180EFE230, which returns at once for a dead
         * player (C_Player slot 38, `C_Actor::IsDead`). Otherwise it stores
         * `value` as the raw axis, and stores it again as the analog axis --
         * shaped by the `wh_horse_*` stick curve (`sign * |v|^exp`, cvar
         * block +0x1A8) while the player rides -- and raises the analog flag.
         * `PreUpdate` copies the analog axes over the move vector and sets the
         * moving bit from them. Neither handler reads the entity id or the
         * activation mode; the action is only the binding's.
         */
        bool OnXiMoveX(const SDK::CCryName &action, float value) {
            return hook::this_call<bool>(SDK::gPatterns.C_PlayerInput__OnXiMoveX, this, SDK::EntityId {0}, &action, 0, value);
        }
        bool OnXiMoveY(const SDK::CCryName &action, float value) {
            return hook::this_call<bool>(SDK::gPatterns.C_PlayerInput__OnXiMoveY, this, SDK::EntityId {0}, &action, 0, value);
        }

        const SDK::Vec3 &GetMovementInput() const {
            return m_movementInput;
        }

        /**
         * The axes as the player pressed them, before the riding stick curve.
         *
         * Written by every move handler, digital and analog, and read back by
         * `C_RiderPlayerInput::BuildHorseMovementRequest` (0x180A4F304), which
         * asks for forward at Y >= 1 and backward at Y <= -1 alongside the
         * rider's stick. On foot it equals the move vector.
         */
        const SDK::Vec2 &GetRawMovementInput() const {
            return m_rawMovementInput;
        }

        /** Whether an analog stick drives the move vector; the walk floor is then skipped. */
        bool IsAnalogMovement() const {
            return m_analogMovement != 0;
        }
        std::uint32_t GetActionRequestFlags() const {
            return m_actionRequestFlags;
        }

      private:
        C_Player *m_pPlayer;                              // 0x10
        std::uint32_t m_actionRequestFlags;               // 0x18, initialized to 8
        std::uint32_t m_previousActionRequestFlags;       // 0x1C, OnAction copies 0x18 here on entry
        PlayerInputActiveActionSetLayout m_activeActions; // 0x20
        std::byte m_opaque0030[0x28];                     // 0x30 opens with std::deque<std::function<void()>>
        SDK::Vec3 m_movementInput;                        // 0x58, OnMove input axes, consumed by PreUpdate
        SDK::Vec3 m_listenedMovementInput;                // 0x64, the move vector as the movement-input listeners left it, stored by PreUpdate
        SDK::Vec3 m_analogMovementInput;                  // 0x70, OnXiMoveX/Y's axes, copied over the move vector while m_analogMovement is set
        std::uint8_t m_runMode;                           // 0x7C, initialized to 1 (jog)
        std::byte m_padding007D[0x3];
        std::byte m_opaque0080[0x30]; // 0x80, holds the look and move axis state
        // Set by OnXiMoveAxis while the movement stick is deflected. PreUpdate
        // copies the analog axis over the move vector while it is set, and the
        // walk floor is skipped.
        std::uint8_t m_analogMovement; // 0xB0
        std::byte m_padding00B1[0x3];
        std::byte m_opaque00B4[0x28];  // 0xB4
        float m_sprintPressTime;       // 0xDC
        std::byte m_opaque00E0[0xB8];  // 0xE0
        SDK::CryStringT<char> m_string0198; // 0x198, built empty
        std::byte m_opaque01A0[0xF4];       // 0x1A0
        SDK::Vec2 m_rawMovementInput;       // 0x294, every move handler's raw axes
        std::byte m_opaque029C[0xC];        // 0x29C

        // CryEngine's TActionHandler<C_PlayerInput>: the action-id to member
        // handler map the constructor fills with OnMoveForward, OnSprint,
        // OnToggleRun, OnToggleCrouch, OnSurrender and the rest.
        std::byte m_actionHandler[0x10]; // 0x2A8
        std::uint16_t m_state02B8;       // 0x2B8
        std::byte m_padding02BA[0x6];

        // The movement-input listeners PreUpdate feeds each frame, a
        // vector of listener pointers -- C_RiderPlayerInput is one.
        void **m_movementInputListeners;         // 0x2C0
        void **m_movementInputListenersEnd;      // 0x2C8
        void **m_movementInputListenersCapacity; // 0x2D0

        static void AssertLayout() {
            static_assert(offsetof(C_PlayerInput, m_movementInput) == 0x58, "C_PlayerInput::m_movementInput offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_pPlayer) == 0x10, "C_PlayerInput::m_pPlayer offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_actionRequestFlags) == 0x18, "C_PlayerInput::m_actionRequestFlags offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_previousActionRequestFlags) == 0x1C, "C_PlayerInput::m_previousActionRequestFlags offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_activeActions) == 0x20, "C_PlayerInput::m_activeActions offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_listenedMovementInput) == 0x64, "C_PlayerInput::m_listenedMovementInput offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_analogMovementInput) == 0x70, "C_PlayerInput::m_analogMovementInput offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_rawMovementInput) == 0x294, "C_PlayerInput::m_rawMovementInput offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_runMode) == 0x7C, "C_PlayerInput::m_runMode offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_analogMovement) == 0xB0, "C_PlayerInput::m_analogMovement offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_sprintPressTime) == 0xDC, "C_PlayerInput::m_sprintPressTime offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_string0198) == 0x198, "C_PlayerInput::m_string0198 offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_actionHandler) == 0x2A8, "C_PlayerInput::m_actionHandler offset mismatch");
            static_assert(offsetof(C_PlayerInput, m_movementInputListeners) == 0x2C0, "C_PlayerInput::m_movementInputListeners offset mismatch");
        }
    };

    static_assert(sizeof(C_PlayerInput) == 0x2D8, "C_PlayerInput size mismatch");
} // namespace SDK::wh::entitymodule
