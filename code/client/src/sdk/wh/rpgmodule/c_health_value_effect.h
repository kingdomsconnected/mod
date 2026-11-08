#pragma once

#include "c_soul_state_effect.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * The effect every event-driven health change is applied through, and so
     * the cause of every one of them. Constructor 0x180C66228, 0x48 bytes.
     *
     * The hit calculation fills the three fields that make it a record of a
     * blow: `SetAttacker180726FF0` the attacker, `SetEffectReasons180726FD0`
     * the reason, and `ResolveVictimDamageEffects` (at 0x180C64131) the
     * subpart the blow resolved to. Fall damage writes its reason directly,
     * and so does `KillByHealthDrain182D28A7C` -- the only effect a bleed,
     * poison or hunger drain ever produces, because regeneration writes its
     * ticks with no cause at all.
     */
    class C_HealthValueEffect: public C_SoulStateEffect {
      public:
        /**
         * The cause of a Health write, seen as what it is.
         *
         * Not a guess: `C_SoulStateStorage::SetState` (0x18046F6CC) itself
         * reads the attacker and reason off any non-null cause of a Health
         * write to attribute the kill, so the engine already stakes the death
         * of every soul on this being the type. Null for any other state or
         * for no cause.
         */
        static const C_HealthValueEffect *FromCause(E_SoulState state, const C_SoulStateEffect *cause) {
            return state == E_SoulState::Health ? static_cast<const C_HealthValueEffect *>(cause) : nullptr;
        }

        /** A `body_subpart.xml` id, or -1 when the damage has no place on the body. */
        std::int32_t GetHitSubpart() const {
            return m_hitSubpart;
        }

        /** The soul whose blow it was; the engine's invalid WUID when nobody's. */
        std::uint64_t GetAttackerSoulWuid() const {
            return m_attackerSoulWuid;
        }

        /** The death-reason number, spelled by the table at 0x1817CADDC. */
        std::uint32_t GetDeathReason() const {
            return m_deathReason;
        }

      private:
        std::int32_t m_hitSubpart; // 0x30
        std::byte m_padding0034[0x4]; // ctor 0x180C66228 writes nothing here
        std::uint64_t m_attackerSoulWuid; // 0x38
        bool m_killedSoul;                // 0x40, set by Apply when this write crossed alive to dead
        std::byte m_padding0041[0x3];
        std::uint32_t m_deathReason; // 0x44

        static void AssertLayout() {
            static_assert(offsetof(C_HealthValueEffect, m_hitSubpart) == 0x30, "C_HealthValueEffect::m_hitSubpart offset mismatch");
            static_assert(offsetof(C_HealthValueEffect, m_attackerSoulWuid) == 0x38, "C_HealthValueEffect::m_attackerSoulWuid offset mismatch");
            static_assert(offsetof(C_HealthValueEffect, m_killedSoul) == 0x40, "C_HealthValueEffect::m_killedSoul offset mismatch");
            static_assert(offsetof(C_HealthValueEffect, m_deathReason) == 0x44, "C_HealthValueEffect::m_deathReason offset mismatch");
        }
    };

    static_assert(sizeof(C_HealthValueEffect) == 0x48, "C_HealthValueEffect size mismatch");
} // namespace SDK::wh::rpgmodule
