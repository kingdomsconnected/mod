#pragma once

#include <cstddef>

namespace SDK::wh::combatmodule {
    class C_CombatActor;

    /**
     * A fighter's RPG side, held at `C_CombatActor +0x388`.
     *
     * The constructor (called from `C_CombatActor::Init`, 0x60 bytes) stores
     * the owning combat actor at +0x08 itself rather than through
     * C_CombatActorObject, and its own table holds only a destructor. Its
     * members are non-virtual: `IsHostileTo` (0x18082A334), which the local
     * player's opponent picker asks, and `GetAttackSpeedMultiplier`
     * (0x180D4DD2C), the time scale of this fighter's swing timeline.
     */
    class C_CombatRPG {
      public:
        virtual ~C_CombatRPG() = default;

        C_CombatActor *GetCombatActor() const {
            return m_pCombatActor;
        }

      private:
        C_CombatActor *m_pCombatActor; // 0x08
        std::byte m_hashMap0010[0x40]; // 0x10; std::unordered_map (sub_1806030E4: load factor 1.0, bucket mask 7/8)
        std::byte m_tierMultipliers[0x10]; // 0x50; std::map {head, size}; ctor inserts tiers 2/3/4 -> 0.0/0.5/1.0

        static void AssertLayout() {
            static_assert(offsetof(C_CombatRPG, m_pCombatActor) == 0x08, "C_CombatRPG::m_pCombatActor offset mismatch");
            static_assert(offsetof(C_CombatRPG, m_tierMultipliers) == 0x50, "C_CombatRPG::m_tierMultipliers offset mismatch");
        }
    };

    // C_CombatActor::Init allocates exactly this many bytes for it.
    static_assert(sizeof(C_CombatRPG) == 0x60, "C_CombatRPG size mismatch");
} // namespace SDK::wh::combatmodule
