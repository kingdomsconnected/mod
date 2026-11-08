#pragma once

#include "../../cryengine/entitysystem/i_entity.h"
#include "../../patterns.h"
#include "c_combat_actor_updated_object.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Who this fighter is squared up against, held at `C_CombatActor +0x3B8`.
     *
     * Built by `C_CombatActor::Init` as a `C_CombatActorUpdatedObject` of kind
     * 4 (0x180917A64), so the owning combat actor sits at +0x08 and is read
     * through the shared base rather than re-declared here.
     *
     * This is the object the whole melee pipeline hangs off. Every contact
     * test opens by reading the opponent this manager publishes at
     * `C_CombatActorModel +0xCF8`, and answers "no contact" without one --
     * `C_CombatActorAnimCollisions::ProcessHit` (0x180D530D8) returns before
     * it has looked at a single bone. A body with no opponent cannot land a
     * melee blow on anything, however good its animation looks.
     */
    class C_CombatActorOpponentManager: public C_CombatActorUpdatedObject {
      public:
        ~C_CombatActorOpponentManager() override = default;

        /**
         * Squares this fighter up against `entity`, or stands it down for null.
         *
         * Writes the weak handle at `C_CombatActorModel +0xCE8` and the raw
         * pointer at +0xCF8, and moves the engine's own teardown subscription
         * from the old opponent's combat actor to the new one -- which is what
         * clears the pointer again when the target leaves the world, and why
         * nothing here has to unwind a pairing by hand.
         *
         * The only gate on the candidate is `C_CombatActor::IsInitialised`
         * (primary vtable slot 89). There is no faction or relation test, so
         * two bodies of the same faction -- two players -- may be set as each
         * other's opponent. The candidate's combat side is built on demand
         * through `C_Actor::GetOrCreateCombatActor`, which still refuses a
         * class whose `CombatOpponentMnTag` is empty.
         *
         * Already-current requests are dropped by the engine itself: the
         * native body opens by comparing `entity` against the opponent it
         * already holds.
         */
        void SetOpponent(IEntity *entity) {
            hook::this_call<void>(gPatterns.C_CombatActorOpponentManager__SetOpponent, this, entity);
        }

      private:
        // Constructor 0x180917A64 builds two 0x10-byte opponent records
        // through `vector constructor iterator` with 0x181950AB0, which
        // zeroes the pointer and the byte, then zeroes the count.
        struct S_OpponentRecord {
            void *m_pValue;               // 0x00, null by default
            std::uint8_t m_flag;          // 0x08, false by default
            std::byte m_padding0009[0x7]; // 0x09
        };

        S_OpponentRecord m_records[2]; // 0x18
        std::uint32_t m_recordCount;   // 0x38, 0 by default
        std::byte m_padding003C[0x4];  // 0x3C

        static void AssertLayout() {
            static_assert(sizeof(S_OpponentRecord) == 0x10, "C_CombatActorOpponentManager::S_OpponentRecord size mismatch");
            static_assert(offsetof(C_CombatActorOpponentManager, m_records) == 0x18, "C_CombatActorOpponentManager::m_records offset mismatch");
            static_assert(offsetof(C_CombatActorOpponentManager, m_recordCount) == 0x38, "C_CombatActorOpponentManager::m_recordCount offset mismatch");
        }
    };

    // C_CombatActor::Init allocates exactly this many bytes for the manager.
    static_assert(sizeof(C_CombatActorOpponentManager) == 0x40, "C_CombatActorOpponentManager size mismatch");
} // namespace SDK::wh::combatmodule
