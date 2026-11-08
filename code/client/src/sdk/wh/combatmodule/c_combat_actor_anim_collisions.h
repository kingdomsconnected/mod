#pragma once

#include "../shared/c_signal.h"
#include "c_combat_actor_updated_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Where a swing becomes a melee contact, held at `C_CombatActor +0x220`.
     *
     * Built by `C_CombatActor::Init` as a `C_CombatActorUpdatedObject` of kind
     * 1 (0x180917708), so the owning combat actor is at +0x08 and is read
     * through the shared base.
     *
     * `UpdatePendingHit` (0x180D53094, vtable slot 6) raises one contact
     * through `ProcessHit` (0x180D530D8) once the pending latch is set and the
     * combat model's countdown at +0x450 has reached zero, so the contact
     * instant is authored per attack by the clip that armed it rather than
     * sampled per frame. `ProcessHit` reads the opponent at
     * `C_CombatActorModel + 0xCF8` and returns without one.
     *
     * With one it asks `TryToFindCollisionInfo` (0x180D5319C) for the contact:
     * either the closest approach of this actor's weapon segment and the
     * opponent's weapon or body segment, or -- when the forced byte is set --
     * a point read straight off a joint of this actor's own character, the hit
     * a synchronized or clinch attack is not allowed to miss. The filled
     * record is broadcast on the signal at +0x18.
     *
     * `C_CombatActionHelperAttack::Enter` (0x180938FD0) subscribes the
     * attack's `C_CombatActorCollisionProcess` to that signal, so this object
     * is wired into every ordinary swing and not only the synchronized ones.
     * The subscriber takes its victim from the broadcast record and reads the
     * opponent only to flag whether that victim was the designated one.
     *
     * The two bytes and the counter below are named by the engine's own debug
     * describe operation, which prints "Enabled: %s, HitIndex: %d" from +0x28
     * and +0x2C.
     */
    class C_CombatActorAnimCollisions: public C_CombatActorUpdatedObject {
      public:
        ~C_CombatActorAnimCollisions() override = default;

        /** Whether this actor raises contacts at all right now. */
        bool IsEnabled() const {
            return m_enabled;
        }

        /** Whether a clip has armed a contact that has not been raised yet. */
        bool IsHitPending() const {
            return m_hitPending;
        }

        /** Whether the next contact skips the geometry and cannot miss. */
        bool IsForcedContact() const {
            return m_forcedContact;
        }

        /** Counts the contacts this actor has raised; stamped into each one. */
        std::uint32_t GetHitIndex() const {
            return m_hitIndex;
        }

      private:
        SDK::wh::shared::C_Signal m_contactSignal; // 0x18, dispatch 0x1856667C0; broadcasts the contact record
        bool m_enabled;               // 0x28
        bool m_hitPending;            // 0x29
        bool m_forcedContact;         // 0x2A
        std::byte m_padding002B[0x1]; // 0x2B
        std::uint32_t m_hitIndex;     // 0x2C
        std::uint32_t m_value0030;    // 0x30, 0 by default (constructor 0x180917708)
        std::byte m_padding0034[0x4]; // 0x34
        std::uint64_t m_value0038;    // 0x38, 0 by default

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorAnimCollisions, m_enabled) == 0x28, "C_CombatActorAnimCollisions::m_enabled offset mismatch");
            static_assert(offsetof(C_CombatActorAnimCollisions, m_hitPending) == 0x29, "C_CombatActorAnimCollisions::m_hitPending offset mismatch");
            static_assert(offsetof(C_CombatActorAnimCollisions, m_forcedContact) == 0x2A, "C_CombatActorAnimCollisions::m_forcedContact offset mismatch");
            static_assert(offsetof(C_CombatActorAnimCollisions, m_hitIndex) == 0x2C, "C_CombatActorAnimCollisions::m_hitIndex offset mismatch");
            static_assert(offsetof(C_CombatActorAnimCollisions, m_contactSignal) == 0x18, "C_CombatActorAnimCollisions::m_contactSignal offset mismatch");
            static_assert(offsetof(C_CombatActorAnimCollisions, m_value0038) == 0x38, "C_CombatActorAnimCollisions::m_value0038 offset mismatch");
        }
    };

    // C_CombatActor::Init allocates exactly this many bytes for the object.
    static_assert(sizeof(C_CombatActorAnimCollisions) == 0x40, "C_CombatActorAnimCollisions size mismatch");
} // namespace SDK::wh::combatmodule
