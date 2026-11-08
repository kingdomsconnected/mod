#pragma once

#include "c_combat_actor_updated_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Paired close-contact actions. Init (0x180916ECC) allocates 0x28 bytes;
     * the constructor (0x180917980) derives from UpdatedObject with kind 6.
     * Its shared alignment gate is used by proximity, contact and attack
     * selection. Ownership is inherited at +0x08.
     *
     * The constructor zeroes +0x18 and +0x1C, writes the "never" timestamp
     * -100000 into +0x20, and subscribes OnClinchStateChanged (0x18090F7EC)
     * to the owning combat model's signal at model +0x108.
     */
    class C_CombatActorClinch: public C_CombatActorUpdatedObject {
      public:
        ~C_CombatActorClinch() override = default;

      private:
        std::uint32_t m_state0018;     // 0x18, 0 by default
        std::uint8_t m_pending001C;    // 0x1C, false by default
        std::byte m_padding001D[0x3];  // 0x1D
        std::int64_t m_timestamp0020;  // 0x20, -100000 (never) by default

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorClinch, m_state0018) == 0x18, "C_CombatActorClinch::m_state0018 offset mismatch");
            static_assert(offsetof(C_CombatActorClinch, m_pending001C) == 0x1C, "C_CombatActorClinch::m_pending001C offset mismatch");
            static_assert(offsetof(C_CombatActorClinch, m_timestamp0020) == 0x20, "C_CombatActorClinch::m_timestamp0020 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorClinch) == 0x28, "C_CombatActorClinch size mismatch");
} // namespace SDK::wh::combatmodule
