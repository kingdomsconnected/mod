#pragma once

#include "../../cryengine/action/i_movement_controller.h"
#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;

    /**
     * The movement controller's locomotion transitions (controller+0x300):
     * starting from a stand, stopping, and turning at speed. Update drives it
     * (sub_1808AA254) and the constructor (sub_180F2F9F4) enables all three.
     *
     * Each kind is reference counted. `SetMoveTransitions` (0x1808FFBC8)
     * moves all three counts by one and sets a kind's bit while its count is
     * above zero, so callers must balance: combat mode's Enter/Leave and the
     * `DisableMoveTransitions` procedural clip and behaviour each pair a
     * disable with an enable.
     */
    class C_MovementTransitions {
      public:
        static constexpr std::uint8_t kStart = 1;
        static constexpr std::uint8_t kStop  = 2;
        static constexpr std::uint8_t kTurn  = 4;

        /** Whether the start transition is counted on -- the one count every disable moves. */
        bool AreEnabled() const { return m_enableCount[0] > 0; }

        void SetEnabled(bool enabled) {
            hook::this_call<void>(gPatterns.C_ActorMovementController__SetMoveTransitions, this, enabled);
        }

      private:
        C_Actor *m_pActor; // 0x00, set by the controller's constructor
        std::byte m_opaque0008[0x20];
        std::uint8_t m_enabled; // 0x28, kStart | kStop | kTurn while each is counted on
        std::byte m_opaque0029[0x6B];
        std::int32_t m_enableCount[3]; // 0x94, start / stop / turn
        std::uint64_t m_state00A0; // 0xA0, zeroed by the controller's constructor
        std::uint64_t m_state00A8; // 0xA8, zeroed by the controller's constructor

        static void AssertLayout() {
            static_assert(offsetof(C_MovementTransitions, m_enabled) == 0x28, "C_MovementTransitions::m_enabled offset mismatch");
            static_assert(offsetof(C_MovementTransitions, m_enableCount) == 0x94, "C_MovementTransitions::m_enableCount offset mismatch");
        }
    };

    static_assert(sizeof(C_MovementTransitions) == 0xB0, "C_MovementTransitions size mismatch");

    /**
     * Concrete movement controller owned by C_Actor.
     *
     * C_Actor::Init constructs this exact 0xA48-byte object and stores it at
     * C_Actor+0x180. RTTI proves IMovementController as the sole base. The
     * constructor stores its actor owner at +0x08, while GetMovementState
     * copies the synchronized 0xF4-byte cache at +0x1F8. Exact-positioning
     * operations own a nullable 0x110-byte helper at +0x2F8; its target is
     * returned by the named interface method rather than exposed directly.
     */
    class C_ActorMovementController : public SDK::IMovementController {
      public:
        ~C_ActorMovementController() override = default;

        bool RequestMovement(SDK::CMovementRequest &request) override;
        void GetMovementState(SDK::SMovementState &state,
                              bool updateState) override;
        bool GetStanceState(const SDK::SStanceStateQuery &query,
                            SDK::SStanceState &state) override;
        SDK::Vec2 GetDesiredMoveDir() const override;
        void SetExactPositioningListener(
            SDK::IExactPositioningListener *listener) override;
        const SDK::SExactPositioningTarget *
        GetExactPositioningTarget() override;
        void unk_fn0007() override;
        void unk_fn0008() override;
        void unk_fn0009() override;
        void unk_fn0010() override;
        void unk_fn0011() override;
        void unk_fn0012() override;
        void unk_fn0013() override;
        void unk_fn0014() override;
        void unk_fn0015() override;
        void unk_fn0016() override;

        C_Actor *GetActor() const { return m_pActor; }

        /**
         * What every `RequestMovement` so far has merged into: the request the
         * controller steers by (0x180837E7C writes it field by field, Update
         * at 0x1804B8E88 reads it as `m_state`). Which targets it still holds
         * is what a caller that steered this body has left in it.
         */
        const SDK::CMovementRequest &GetRequestState() const { return m_state; }

        /** This controller's start, stop and turn transitions; see C_MovementTransitions. */
        C_MovementTransitions &GetMoveTransitions() { return m_moveTransitions; }

        // The native GetMovementState method should be preferred when a
        // synchronized snapshot is required.
        /** Whether the last Update resolved an aim target for this frame; see m_frameAim. */
        bool HasFrameAim() const {
            return m_frameAim;
        }

        /** Whether the last Update resolved a look target for this frame. */
        bool HasFrameLook() const {
            return m_frameLook;
        }

        const SDK::SMovementState &GetCachedMovementStateUnsafe() const {
            return m_cachedMovementState;
        }

      private:
        C_Actor *m_pActor;                        // 0x008
        SDK::CMovementRequest m_state;            // 0x010
        std::byte m_state01C0[0x5];               // 0x1C0
        // Update (0x1804B8E88) copies the frame's look and aim flags here
        // at its tail (+0x1C5 = output+0x5C, +0x1C6 = output+0x5D), and
        // keeps the points it resolved: the look point, and the aim point
        // that is the frame's aim target while the aim flag holds and the
        // look point otherwise. UpdateMovementState aims the state from it.
        bool m_frameLook;   // 0x1C5
        bool m_frameAim;    // 0x1C6
        std::byte m_state01C7[0x1];
        SDK::Vec3 m_lookPoint; // 0x1C8
        SDK::Vec3 m_aimPoint;  // 0x1D4
        std::byte m_state01E0[0x18];
        SDK::SMovementState m_cachedMovementState; // 0x1F8
        std::uint32_t m_padding02EC;
        std::uint32_t m_movementStateLock;         // 0x2F0
        std::uint32_t m_padding02F4;
        void *m_pExactPositioningState;            // 0x2F8, owned 0x110 bytes
        C_MovementTransitions m_moveTransitions;   // 0x300
        C_Actor *m_pActorStateOwner;               // 0x3B0
        std::uint32_t m_state03B8;                 // 0x3B8
        std::byte m_state03BC[0x8];                // 0x3BC
        std::uint32_t m_state03C4;                 // 0x3C4
        std::uint64_t m_state03C8;                 // 0x3C8
        std::uint32_t m_state03D0;                 // 0x3D0
        float m_state03D4;                         // 0x3D4, 1e-4 by default
        // Two fixed sample rings, each a count and a hundred 8-byte entries
        // built by 0x1823D75E8.
        std::uint32_t m_sampleCountA;              // 0x3D8
        std::byte m_samplesA[100][0x8];            // 0x3DC
        std::uint32_t m_sampleCountB;              // 0x6FC
        std::byte m_samplesB[100][0x8];            // 0x700
        std::int32_t m_state0A20;                  // 0xA20, initialized -1
        std::uint32_t m_state0A24;
        std::uint32_t m_actorState0A28;
        std::uint32_t m_padding0A2C;
        void *m_state0A30;
        void *m_state0A38;
        void *m_state0A40;

        static void AssertLayout() {
            static_assert(offsetof(C_ActorMovementController, m_state) == 0x10, "C_ActorMovementController::m_state offset mismatch");
            static_assert(offsetof(C_ActorMovementController, m_frameAim) == 0x1C6, "C_ActorMovementController::m_frameAim offset mismatch");
            static_assert(offsetof(C_ActorMovementController, m_aimPoint) == 0x1D4, "C_ActorMovementController::m_aimPoint offset mismatch");
            static_assert(offsetof(C_ActorMovementController, m_cachedMovementState) == 0x1F8, "C_ActorMovementController::m_cachedMovementState offset mismatch");
            static_assert(offsetof(C_ActorMovementController, m_moveTransitions) == 0x300, "C_ActorMovementController::m_moveTransitions offset mismatch");
            static_assert(offsetof(C_ActorMovementController, m_state03D4) == 0x3D4, "C_ActorMovementController::m_state03D4 offset mismatch");
            static_assert(offsetof(C_ActorMovementController, m_sampleCountB) == 0x6FC, "C_ActorMovementController::m_sampleCountB offset mismatch");
            static_assert(offsetof(C_ActorMovementController, m_state0A20) == 0xA20, "C_ActorMovementController::m_state0A20 offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorMovementController) == 0xA48,
                  "C_ActorMovementController size mismatch");
} // namespace SDK::wh::entitymodule
