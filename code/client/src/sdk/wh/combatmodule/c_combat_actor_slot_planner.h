#pragma once

#include "c_combat_actor_updated_object.h"

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * What choreographs a KCD2 fight, held at `C_CombatActor +0x3E0`.
     *
     * Built by `C_CombatActor::Init` as a `C_CombatActorUpdatedObject`
     * (0x180917C68), so the owning combat actor is at +0x08 and is read
     * through the shared base. The scheduled entries live in an intrusive list
     * from +0x18, which is why `Update` hands that address in as the receiver
     * rather than the planner itself.
     *
     * `Update` (0x1813C494C, vtable slot 7) runs only while the owner has an
     * opponent, and calls `DispatchDueSlots` (0x181E3D7B0) with
     * `(RCX = &entries, RDX = the owner's model, R8 = the planner)`. That
     * function walks the entries and, for each whose timer has expired, looks
     * its kind up on the **opponent's** `C_CombatActorDirector` and posts it
     * there.
     *
     * So the attacker schedules the defender's reaction. That is what makes
     * single-player combat read as a duel rather than as two independent
     * fencers, and it is what `wh_cs_DelayedBlock`,
     * `wh_cs_DelayedPerfectBlockInSlot`, `wh_cs_DelayedMasterStrikeInSlot` and
     * `wh_cs_DelayedRiposteInSlot` tune. Between two players it is the wrong
     * machine deciding: one peer would drive the other's defence.
     */
    class C_CombatActorSlotPlanner: public C_CombatActorUpdatedObject {
      public:
        ~C_CombatActorSlotPlanner() override = default;

      private:
        // The scheduled entries: an MSVC std::unordered_map laid out from
        // +0x18, as constructor 0x180917C68 builds it -- max load factor
        // 1.0, a list with a 0x28-byte sentinel, the bucket vector grown by
        // _Assign_grow to 16 iterators, mask 7 and max index 8.
        float m_maxLoadFactor;            // 0x18, 1.0 by default
        std::byte m_padding001C[0x4];     // 0x1C
        void *m_pEntryListHead;           // 0x20
        std::uint64_t m_entryCount;       // 0x28
        SDK::StdVector<void *> m_buckets; // 0x30, list iterators, two per bucket
        std::uint64_t m_bucketMask;       // 0x48, 7 by default
        std::uint64_t m_bucketMaxIndex;   // 0x50, 8 by default

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorSlotPlanner, m_maxLoadFactor) == 0x18, "C_CombatActorSlotPlanner::m_maxLoadFactor offset mismatch");
            static_assert(offsetof(C_CombatActorSlotPlanner, m_pEntryListHead) == 0x20, "C_CombatActorSlotPlanner::m_pEntryListHead offset mismatch");
            static_assert(offsetof(C_CombatActorSlotPlanner, m_entryCount) == 0x28, "C_CombatActorSlotPlanner::m_entryCount offset mismatch");
            static_assert(offsetof(C_CombatActorSlotPlanner, m_buckets) == 0x30, "C_CombatActorSlotPlanner::m_buckets offset mismatch");
            static_assert(offsetof(C_CombatActorSlotPlanner, m_bucketMask) == 0x48, "C_CombatActorSlotPlanner::m_bucketMask offset mismatch");
        }
    };

    // C_CombatActor::Init allocates exactly this many bytes for the planner.
    static_assert(sizeof(C_CombatActorSlotPlanner) == 0x58, "C_CombatActorSlotPlanner size mismatch");
} // namespace SDK::wh::combatmodule
