#pragma once

#include "../animationmodule/i_scene_listener.h"
#include "../shared/c_signal.h"
#include "c_combat_actor_collision_process.h"
#include "c_combat_actor_object.h"
#include "c_weapon_proc_solver.h"
#include "c_weapon_solver.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Actor combat-collision coordinator and weapon-solver owner.
     *
     * RTTI proves C_CombatActorObject at +0x00 and
     * animationmodule::I_SceneListener at +0x10. Its primary table at
     * 0x183B91300 and secondary table at 0x183B912E8 contain only their
     * inherited contracts. Construction owns C_WeaponProcSolver and
     * C_WeaponSolver instances at +0x88 and +0x90; scalar deletion proves
     * the exact 0x268-byte layout.
     */
    class C_CombatActorCollisions
        : public C_CombatActorObject
        , public animationmodule::I_SceneListener {
      public:
        ~C_CombatActorCollisions() override = default;

        /**
         * The first contact of the latest sweep with an object and part this
         * attack had not touched yet; invalid when there was none.
         * `SweepWeaponShape` (0x180A7A124) clears it before each sweep, and
         * `OnShapeContact` (0x180D51854) fills it only for a key not yet in
         * the attack's set (+0x258), which `Start` (0x180D4F93C) empties.
         */
        const S_CombatContact &GetFirstContact() const {
            return m_firstContact;
        }

      private:
        // Constructor 0x181481B9C writes everything below.
        SDK::wh::shared::C_Signal m_signal0018; // 0x18, dispatch 0x185666C28
        SDK::wh::shared::C_Signal m_signal0028; // 0x28, dispatch 0x1855D56F8
        std::uint64_t m_value0038;              // 0x38, 0 by default
        std::uint16_t m_value0040;              // 0x40, 0 by default
        std::uint8_t m_flag0042;                // 0x42, false by default
        std::byte m_padding0043[0x1];           // 0x43
        std::uint32_t m_value0044;              // 0x44, 2 by default
        std::uint32_t m_value0048;              // 0x48, 0 by default
        std::byte m_padding004C[0x4];           // 0x4C
        std::uint64_t m_value0050;              // 0x50, 0 by default
        // Two std::set<void *> built by `vector constructor iterator`
        // (count 2, stride 0x10): a sentinel head and an element count each.
        void *m_pSetHead0058;                   // 0x58
        std::uint64_t m_setSize0060;            // 0x60
        void *m_pSetHead0068;                   // 0x68
        std::uint64_t m_setSize0070;            // 0x70
        std::uint64_t m_value0078;              // 0x78, 0 by default
        std::uint64_t m_value0080;              // 0x80, 0 by default
        C_WeaponProcSolver *m_pWeaponProcSolver; // 0x88, 0x3C8 bytes, constructor 0x181481E18
        C_WeaponSolver *m_pWeaponSolver;         // 0x90, 0x50 bytes, constructor 0x181481D80
        // The latest sweep's earliest contact, which `SweepWeaponShape`
        // returns, and its earliest one with something new to this attack.
        S_CombatContact m_closestContact; // 0x98
        S_CombatContact m_firstContact;   // 0x170
        // Two MSVC std::map heads: a 0x28-byte sentinel node (flags 0x0101)
        // and an element count each.
        void *m_pMapHead0248;                   // 0x248
        std::uint64_t m_mapSize0250;            // 0x250
        void *m_pMapHead0258;                   // 0x258
        std::uint64_t m_mapSize0260;            // 0x260

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorCollisions, m_signal0018) == 0x18, "C_CombatActorCollisions::m_signal0018 offset mismatch");
            static_assert(offsetof(C_CombatActorCollisions, m_signal0028) == 0x28, "C_CombatActorCollisions::m_signal0028 offset mismatch");
            static_assert(offsetof(C_CombatActorCollisions, m_value0044) == 0x44, "C_CombatActorCollisions::m_value0044 offset mismatch");
            static_assert(offsetof(C_CombatActorCollisions, m_pSetHead0058) == 0x58, "C_CombatActorCollisions::m_pSetHead0058 offset mismatch");
            static_assert(offsetof(C_CombatActorCollisions, m_pWeaponProcSolver) == 0x88, "C_CombatActorCollisions::m_pWeaponProcSolver offset mismatch");
            static_assert(offsetof(C_CombatActorCollisions, m_pWeaponSolver) == 0x90, "C_CombatActorCollisions::m_pWeaponSolver offset mismatch");
            static_assert(offsetof(C_CombatActorCollisions, m_closestContact) == 0x98, "C_CombatActorCollisions::m_closestContact offset mismatch");
            static_assert(offsetof(C_CombatActorCollisions, m_firstContact) == 0x170, "C_CombatActorCollisions::m_firstContact offset mismatch");
            static_assert(offsetof(C_CombatActorCollisions, m_pMapHead0248) == 0x248, "C_CombatActorCollisions::m_pMapHead0248 offset mismatch");
            static_assert(offsetof(C_CombatActorCollisions, m_pMapHead0258) == 0x258, "C_CombatActorCollisions::m_pMapHead0258 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorCollisions) == 0x268, "C_CombatActorCollisions size mismatch");
} // namespace SDK::wh::combatmodule
