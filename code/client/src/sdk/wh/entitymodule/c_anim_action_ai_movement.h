#pragma once

#include "c_base_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;

    /**
     * The Mannequin action that owns an actor's locomotion.
     *
     * Its state selector answers an index into the fragment table it built
     * from SMannequinActorMovementParams: 1 idle, 2 turn, 3 big turn,
     * 4 movement, 5 in-air. Everything a body does while walking comes out of
     * that one number.
     *
     * The selector reads the owner back-pointer first to reach the actor
     * whose gate and velocity it tests. `C_Actor::EnsureLocomotionAction`
     * allocates 0x180 bytes for it, which pins the total size; the
     * C_BaseAction prefix (ctor 0x180F45520 calls C_BaseAction 0x180F45D20)
     * carries the Mannequin status at +0x28.
     */
    class CAnimActionAIMovement: public C_BaseAction {
      public:
        /** Values the state selector answers, as fragment-table indices. */
        enum EState : std::uint32_t {
            STATE_IDLE     = 1,
            STATE_TURN     = 2,
            STATE_TURN_BIG = 3,
            STATE_MOVEMENT = 4,
            STATE_IN_AIR   = 5,
        };

        C_Actor *GetOwnerActor() const {
            return m_pOwnerActor;
        }

        /**
         * This action's `IAction::EStatus`. Zero means it is not installed, which
         * is what `C_Actor::EnsureLocomotionAction` (0x1804ABB07) tests before
         * building a replacement.
         */
        std::uint32_t GetStatus() const { return static_cast<std::uint32_t>(C_BaseAction::GetStatus()); }

        /**
         * The state whose fragment is playing: `OnFragmentStarted` (0x180737DA4)
         * commits the selected one here once its fragment has actually started,
         * so a turn the selector only asked for does not read as one yet.
         */
        EState GetPlayingState() const { return static_cast<EState>(m_playingState); }

        /** The state `Update` (0x1804ACED0) last selected, before its fragment has started. */
        EState GetSelectedState() const { return static_cast<EState>(m_selectedState); }

      private:
        C_Actor *m_pOwnerActor;        // 0x98
        std::uint32_t m_selectedState; // 0xA0, written by Update (0x1804ACED0) as it sets the fragment
        std::uint32_t m_playingState;  // 0xA4
        // Ctor 0x180F45520 (allocated 0x180 bytes by C_Actor::EnsureLocomotionAction 0x1804ABAE0).
        std::byte m_entries00A8[6][0x8]; // 0xA8, six 8-byte entries built by 0x18152C4D0
        void *m_value00D8;             // 0xD8, ctor zeroes
        std::byte m_params00E0[0xC];   // 0xE0, the 12 bytes the ctor copies from its third argument
        std::byte m_padding00EC[0x4];
        void *m_value00F0;             // 0xF0, ctor zeroes
        std::int64_t m_value00F8;      // 0xF8, ctor sets -1
        float m_values0100[6][5];      // 0x100, six 20-byte records, ctor zeroes
        std::int32_t m_value0178;      // 0x178, ctor sets -1
        std::uint8_t m_flag017C;       // 0x17C, ctor zeroes
        std::byte m_padding017D[0x3];

        static void AssertLayout() {
            static_assert(offsetof(CAnimActionAIMovement, m_pOwnerActor) == 0x98, "CAnimActionAIMovement::m_pOwnerActor offset mismatch");
            static_assert(offsetof(CAnimActionAIMovement, m_selectedState) == 0xA0, "CAnimActionAIMovement::m_selectedState offset mismatch");
            static_assert(offsetof(CAnimActionAIMovement, m_playingState) == 0xA4, "CAnimActionAIMovement::m_playingState offset mismatch");
            static_assert(offsetof(CAnimActionAIMovement, m_entries00A8) == 0xA8, "CAnimActionAIMovement::m_entries00A8 offset mismatch");
            static_assert(offsetof(CAnimActionAIMovement, m_params00E0) == 0xE0, "CAnimActionAIMovement::m_params00E0 offset mismatch");
            static_assert(offsetof(CAnimActionAIMovement, m_values0100) == 0x100, "CAnimActionAIMovement::m_values0100 offset mismatch");
            static_assert(offsetof(CAnimActionAIMovement, m_flag017C) == 0x17C, "CAnimActionAIMovement::m_flag017C offset mismatch");
        }
    };

    static_assert(sizeof(CAnimActionAIMovement) == 0x180, "CAnimActionAIMovement size mismatch");

    /**
     * What `C_Actor+0x738` points at: the holder of an actor's locomotion
     * action, not the action itself. `C_Actor::EnsureLocomotionAction`
     * (0x1804ABAE0) is handed this pointer, reads the action from its first
     * field and writes a freshly built one back there; `GetAIAnimationStateSpeed`
     * (slot 134, 0x180645870) reads the pace at +0x18. The type's own name is
     * not recovered; only those two fields are claimed.
     */
    struct C_AIAnimationState {
        CAnimActionAIMovement *m_pAction; // 0x00
        std::byte m_opaque0008[0x10];

        /**
         * The pace the locomotion is playing at, as an `E_MovementSpeed`
         * ordinal: the single source of the MoveSpeed animation tag, written by
         * `C_ActorMovementController::Update` from the movement request's
         * pseudo-speed.
         */
        float m_pseudoSpeed; // 0x18

      private:
        static void AssertLayout() {
            static_assert(offsetof(C_AIAnimationState, m_pAction) == 0x0, "C_AIAnimationState::m_pAction offset mismatch");
            static_assert(offsetof(C_AIAnimationState, m_pseudoSpeed) == 0x18, "C_AIAnimationState::m_pseudoSpeed offset mismatch");
        }
    };
} // namespace SDK::wh::entitymodule
