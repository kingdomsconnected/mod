#pragma once

#include "c_combat_actor_updated_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * The combat slow-motion, held at `C_CombatActor +0x3C8`.
     *
     * Built by `C_CombatActor::Init` as a `C_CombatActorUpdatedObject` of kind
     * 4 (0x1809177A8), so the owning combat actor is at +0x08 and is read
     * through the shared base. What the rest of the object holds is two
     * transition timestamps, a scale and an enable byte; none of it is needed
     * to decide whether a dilation may start, so none of it is named.
     *
     * `Update` (0x1809D83B4, vtable slot 8) starts a dilation through
     * `BeginTimewarp` (0x1810C0BEC) and ends it through `EndTimewarp`
     * (0x1810C1018). The start is gated on `C_Actor::IsPlayerControlled` for
     * this actor **or its opponent** -- and that predicate is the game
     * object's network channel id, meaning "driven by a player rather than by
     * AI", not "is the local player". Every replicated player body satisfies
     * it, so a fight between two bodies this peer merely watches would scale
     * this peer's global timer. What it scales is global: the start bumps a
     * process-wide refcount and writes the system timer.
     */
    class C_CombatActorTimewarp: public C_CombatActorUpdatedObject {
      public:
        ~C_CombatActorTimewarp() override = default;

      private:
        // Constructor 0x1809177A8 writes every member below.
        std::uint8_t m_active0018;      // 0x18, false by default
        std::byte m_padding0019[0x7];   // 0x19
        std::int64_t m_beginTime0020;   // 0x20, -100000 (never) by default
        std::int64_t m_endTime0028;     // 0x28, -100000 (never) by default
        std::uint32_t m_state0030;      // 0x30, 0 by default
        float m_scale0034;              // 0x34, 1.0 by default
        float m_value0038;              // 0x38, 0 by default
        std::uint32_t m_value003C;      // 0x3C, 0 by default
        std::uint32_t m_value0040;      // 0x40, 0 by default
        std::uint8_t m_enabled0044;     // 0x44, true by default
        std::byte m_padding0045[0x3];   // 0x45

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorTimewarp, m_active0018) == 0x18, "C_CombatActorTimewarp::m_active0018 offset mismatch");
            static_assert(offsetof(C_CombatActorTimewarp, m_beginTime0020) == 0x20, "C_CombatActorTimewarp::m_beginTime0020 offset mismatch");
            static_assert(offsetof(C_CombatActorTimewarp, m_endTime0028) == 0x28, "C_CombatActorTimewarp::m_endTime0028 offset mismatch");
            static_assert(offsetof(C_CombatActorTimewarp, m_scale0034) == 0x34, "C_CombatActorTimewarp::m_scale0034 offset mismatch");
            static_assert(offsetof(C_CombatActorTimewarp, m_enabled0044) == 0x44, "C_CombatActorTimewarp::m_enabled0044 offset mismatch");
        }
    };

    // C_CombatActor::Init allocates exactly this many bytes for the timewarp.
    static_assert(sizeof(C_CombatActorTimewarp) == 0x48, "C_CombatActorTimewarp size mismatch");
} // namespace SDK::wh::combatmodule
