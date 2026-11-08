#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../shared/c_recursive_srw_lock.h"
#include "c_combat_actor_object.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * The clock of a fighter's swing, held at `C_CombatActor +0x3C0`.
     *
     * Built by `C_CombatActor::Init` (0x98 bytes) over C_CombatActorObject, so
     * the owning combat actor sits at +0x08. `BeginAttackTimeline`
     * (0x180D4D950) schedules the start, hit and withdraw of a swing from its
     * attack row divided by `C_CombatRPG::GetAttackSpeedMultiplier`, and
     * resets the model countdowns at +0x3F0, +0x450 and +0x420; the one at
     * +0x450 is what the pending contact waits on. `Update` (0x1809E3B80)
     * runs those countdowns down in game seconds.
     */
    class C_CombatActorTimeCop: public C_CombatActorObject {
      public:
        ~C_CombatActorTimeCop() override = default;

        /**
         * Pushes the running swing's hit later by up to `seconds`, slowing the
         * animation into it so the clip and the +0x450 countdown still meet.
         *
         * The engine's own swing hold: `HandleCombatAnimationEvent` calls it
         * on a clip's "SlotDelay" event, under the combat group lock. Refused
         * without a live hit entry -- so only after BeginAttackTimeline -- and
         * capped per attack by `wh_cs_TimeAlignment_MaxAnimAddition` less what
         * this attack already used. A non-positive `blendSeconds` blends over
         * the whole run up to the hit. Returns the delay actually applied.
         */
        float ApplySlotDelay(float seconds, float blendSeconds) {
            return hook::this_call<float>(gPatterns.C_CombatActorTimeCop__ApplySlotDelay, this, seconds, static_cast<void *>(nullptr), blendSeconds);
        }

      private:
        // Constructor 0x180918000 writes everything below.
        float m_speed0010;                          // 0x10, 1.0 by default
        std::uint8_t m_flag0014;                    // 0x14, false by default
        std::byte m_padding0015[0x3];               // 0x15
        // A 0x20-byte parameter record of a stripped type, initialized by
        // 0x1809181B0: float 1.0 at +0x10, true at +0x16, the rest zero.
        std::byte m_parameters0018[0x20];           // 0x18
        SDK::StdVector<void *> m_entries0038;       // 0x38, 8-byte elements, three reserved
        std::uint64_t m_value0050;                  // 0x50, 0 by default
        // 0x14-byte elements (the reserve divides by 20), three reserved by
        // 0x1816BDC2C: the swing's timeline entries. Raw bounds only.
        SDK::StdVector<std::byte> m_timeline0058;   // 0x58
        std::uint64_t m_value0070;                  // 0x70, 0 by default
        // 0x18091865C builds +0x78..+0x98: two counters, an auto-reset event
        // and the recursive lock that guards the timeline.
        std::uint32_t m_value0078;                  // 0x78, 0 by default
        std::uint32_t m_value007C;                  // 0x7C, 0 by default
        void *m_hEvent0080;                         // 0x80, CreateEventA(nullptr, FALSE, FALSE, nullptr)
        SDK::wh::shared::C_RecursiveSRWLock m_lock; // 0x88

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorTimeCop, m_speed0010) == 0x10, "C_CombatActorTimeCop::m_speed0010 offset mismatch");
            static_assert(offsetof(C_CombatActorTimeCop, m_parameters0018) == 0x18, "C_CombatActorTimeCop::m_parameters0018 offset mismatch");
            static_assert(offsetof(C_CombatActorTimeCop, m_entries0038) == 0x38, "C_CombatActorTimeCop::m_entries0038 offset mismatch");
            static_assert(offsetof(C_CombatActorTimeCop, m_timeline0058) == 0x58, "C_CombatActorTimeCop::m_timeline0058 offset mismatch");
            static_assert(offsetof(C_CombatActorTimeCop, m_hEvent0080) == 0x80, "C_CombatActorTimeCop::m_hEvent0080 offset mismatch");
            static_assert(offsetof(C_CombatActorTimeCop, m_lock) == 0x88, "C_CombatActorTimeCop::m_lock offset mismatch");
        }
    };

    // C_CombatActor::Init allocates exactly this many bytes for it.
    static_assert(sizeof(C_CombatActorTimeCop) == 0x98, "C_CombatActorTimeCop size mismatch");
} // namespace SDK::wh::combatmodule
