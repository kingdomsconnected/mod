#pragma once

#include "../entitymodule/c_actor.h"
#include "c_combat_actor.h"
#include "c_combat_actor_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * What turns a swing's contact into its consequences. 0x20 bytes over
     * C_CombatActorObject (RTTI; allocated by 0x180915010), so the ATTACKING
     * fighter is its owner at +0x08.
     *
     * Its two hit handlers are where every consequence of a melee contact is
     * produced -- the victim's hit reaction, the attacker's FailedAttack, the
     * damage event, the hit record -- all behind `ResolveCollisionHit`
     * (0x180D53610), which they both call. `OnGeometricHit` (0x180C5235C) is
     * the ordinary swing's, activated by `C_CombatActionHelperAttack::
     * EnterStriking`; `OnAnimationHit` (0x180D53778) takes the contacts armed
     * through C_CombatActorAnimCollisions -- sync attacks and sync defenses.
     */
    class C_CombatActorCollisionProcess: public C_CombatActorObject {
      public:
        ~C_CombatActorCollisionProcess() override = default;

      private:
        // Constructor 0x180915010 stores its third argument here; the three
        // creators (0x180915140, 0x1819795D0, 0x181E3B470) pass an address
        // inside the creating object (+0x10 or +0x80), whose type is stripped.
        void *m_pOwnerRecord;         // 0x10
        std::uint8_t m_flag0018;      // 0x18, false by default
        std::byte m_padding0019[0x7]; // 0x19

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorCollisionProcess, m_pOwnerRecord) == 0x10, "C_CombatActorCollisionProcess::m_pOwnerRecord offset mismatch");
            static_assert(offsetof(C_CombatActorCollisionProcess, m_flag0018) == 0x18, "C_CombatActorCollisionProcess::m_flag0018 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorCollisionProcess) == 0x20, "C_CombatActorCollisionProcess size mismatch");

    /**
     * One contact of a weapon's shape with something solid, as
     * `C_CombatActorCollisions` keeps it and `OnGeometricHit` is handed it.
     * 0xD8 bytes, built by 0x180D508C0 and cleared by `Reset` (0x180D50990).
     *
     * `BuildContactRecord` (0x180D51B38) copies the first 0x88 bytes from the
     * raw contact the collision scene reports -- for an armed weapon,
     * `FillWorldContact` (0x1813F5718) fills those from the `geom_contact` --
     * and adds the victim when the entity hit is a combat actor's. A tree,
     * a wall or the ground has none.
     */
    struct S_CombatContact {
        /** Where the weapon met it, in world space. */
        const Vec3 &GetPoint() const {
            return m_point;
        }

        /** The contact normal, the `geom_contact` one negated for an armed weapon and not for a fist; orient it against `GetDirection` before relying on its sign. */
        const Vec3 &GetNormal() const {
            return m_normal;
        }

        /** Which way the weapon was moving, normalized. */
        const Vec3 &GetDirection() const {
            return m_direction;
        }

        /** The entity hit, when what was hit is an entity's (foreign data 2); 0 for a brush, vegetation and the ground. */
        std::uint32_t GetEntityId() const {
            return m_entityId;
        }

        /** The surface type index of what was hit, as `GetSurfaceParameters` takes it. */
        std::int32_t GetSurfaceIndex() const {
            return m_surfaceIndex;
        }

        std::int32_t GetPartId() const {
            return m_partId;
        }

        /** The physical entity hit, for `GetPhysicalEntityById`; `INT32_MIN` for a fist or a kick, which the capsule and sphere tests leave unset. */
        std::int32_t GetPhysicalEntityId() const {
            return m_physicalEntityId;
        }

        /** Whether this contact has been filled in at all. */
        bool IsValid() const {
            return m_valid != 0;
        }

        C_CombatActor *GetVictim() const {
            return m_pVictim;
        }

      private:
        Vec3 m_point;                          // 0x00, geom_contact.pt
        Vec3 m_normal;                         // 0x0C
        Vec3 m_direction;                      // 0x18
        std::uint32_t m_entityId;              // 0x24
        std::byte m_partFrame0028[0x3C];       // 0x28, the hit part's frame
        std::int32_t m_surfaceIndex;           // 0x64, geom_contact +0x38
        std::int32_t m_primitive;              // 0x68
        std::int32_t m_weaponSurface;          // 0x6C, the weapon shape's own
        std::int32_t m_partId;                 // 0x70, geom_contact +0x48
        std::int32_t m_physicalEntityId;       // 0x74, geom_contact +0x44
        std::int32_t m_value0078;              // 0x78
        float m_time;                          // 0x7C, how far along the sweep
        std::int32_t m_jointId;                // 0x80, -1 for none
        float m_speed;                         // 0x84, geom_contact.vel
        std::byte m_opaque0088[0x3C];          // 0x88, shape flags, then the weapon's pose at impact
        std::uint8_t m_new;                    // 0xC4, first contact with this object in this attack
        std::uint8_t m_valid;                  // 0xC5
        std::byte m_padding00C6[0x2];          // 0xC6
        C_CombatActor *m_pVictim;              // 0xC8
        entitymodule::C_Actor *m_pVictimActor; // 0xD0

        static void AssertLayout() {
            static_assert(offsetof(S_CombatContact, m_entityId) == 0x24, "S_CombatContact::m_entityId offset mismatch");
            static_assert(offsetof(S_CombatContact, m_surfaceIndex) == 0x64, "S_CombatContact::m_surfaceIndex offset mismatch");
            static_assert(offsetof(S_CombatContact, m_partId) == 0x70, "S_CombatContact::m_partId offset mismatch");
            static_assert(offsetof(S_CombatContact, m_physicalEntityId) == 0x74, "S_CombatContact::m_physicalEntityId offset mismatch");
            static_assert(offsetof(S_CombatContact, m_time) == 0x7C, "S_CombatContact::m_time offset mismatch");
            static_assert(offsetof(S_CombatContact, m_new) == 0xC4, "S_CombatContact::m_new offset mismatch");
            static_assert(offsetof(S_CombatContact, m_valid) == 0xC5, "S_CombatContact::m_valid offset mismatch");
            static_assert(offsetof(S_CombatContact, m_pVictim) == 0xC8, "S_CombatContact::m_pVictim offset mismatch");
            static_assert(offsetof(S_CombatContact, m_pVictimActor) == 0xD0, "S_CombatContact::m_pVictimActor offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatContact) == 0xD8, "S_CombatContact size mismatch");

    /**
     * `wh::combatmodule::CombatHitInfo` (RTTR, 0x18110CC1C), the record
     * C_CombatActorAnimCollisions::ProcessHit broadcasts and
     * `OnAnimationHit` takes; the victim sits at +0x90.
     */
    struct CombatHitInfo {
        C_CombatActor *GetVictim() const {
            return m_pVictim;
        }

      private:
        // ProcessHit (0x180D530D8) builds the record on its stack: the first
        // 0x88 bytes are initialized by 0x180D508C0 (the same stripped
        // collision record C_CombatActorCollisions embeds) and filled by
        // TryToFindCollisionInfo (0x180D5319C); the rest it stamps itself.
        std::byte m_collision0000[0x80];  // 0x00
        std::int32_t m_value0080;         // 0x80, -1 by default; copied into +0x88
        std::uint32_t m_value0084;        // 0x84, 0 by default
        std::int32_t m_value0088;         // 0x88, the found contact's +0x80, -1 without one
        std::byte m_padding008C[0x4];     // 0x8C
        C_CombatActor *m_pVictim;         // 0x90
        std::uint32_t m_hitIndex;         // 0x98, C_CombatActorAnimCollisions::m_hitIndex, unless forced
        bool m_forced;                    // 0x9C, C_CombatActorAnimCollisions::m_forcedContact
        bool m_notPending;                // 0x9D, !C_CombatActorAnimCollisions::m_hitPending

        static void AssertLayout() {
            static_assert(offsetof(CombatHitInfo, m_value0080) == 0x80, "CombatHitInfo::m_value0080 offset mismatch");
            static_assert(offsetof(CombatHitInfo, m_value0088) == 0x88, "CombatHitInfo::m_value0088 offset mismatch");
            static_assert(offsetof(CombatHitInfo, m_pVictim) == 0x90, "CombatHitInfo::m_pVictim offset mismatch");
            static_assert(offsetof(CombatHitInfo, m_hitIndex) == 0x98, "CombatHitInfo::m_hitIndex offset mismatch");
            static_assert(offsetof(CombatHitInfo, m_forced) == 0x9C, "CombatHitInfo::m_forced offset mismatch");
        }
    };
} // namespace SDK::wh::combatmodule
