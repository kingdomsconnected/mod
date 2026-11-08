#pragma once

#include "../../types.h"
#include "c_geom_manager.h"
#include "i_phys_utils.h"
#include "i_physical_entity.h"
#include "i_physical_entity_it.h"
#include "i_physical_world.h"
#include "physics_vars.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One ray hit, laid out exactly as CryEngine's `ray_hit`. `collider` is
     * the route back to what was struck; the terrain has none and says so in
     * `isTerrain` instead.
     */
    struct SRayHit {
        float distance;                     // 0x00
        std::byte padding0004[0x04];        // 0x04
        void *collider;                     // 0x08, an IPhysicalEntity
        std::int32_t part;                  // 0x10
        std::int32_t partId;                // 0x14
        std::int16_t surfaceIndex;          // 0x18
        std::int16_t originalMaterialIndex; // 0x1A, before material mapping
        std::int32_t foreignIndex;          // 0x1C
        std::int32_t treeNode;              // 0x20, the BV node that hit
        Vec3 point;                         // 0x24
        Vec3 normal;                        // 0x30
        std::int32_t isTerrain;             // 0x3C
        std::int32_t primitive;             // 0x40, the triangle index
        std::byte padding0044[0x4];         // 0x44
        void *next;                         // 0x48, the engine's own chain
    };

    static_assert(sizeof(SRayHit) == 0x50, "SRayHit size mismatch");
    static_assert(offsetof(SRayHit, collider) == 0x08, "SRayHit::collider offset mismatch");
    static_assert(offsetof(SRayHit, surfaceIndex) == 0x18, "SRayHit::surfaceIndex offset mismatch");
    static_assert(offsetof(SRayHit, point) == 0x24, "SRayHit::point offset mismatch");
    static_assert(offsetof(SRayHit, normal) == 0x30, "SRayHit::normal offset mismatch");
    static_assert(offsetof(SRayHit, isTerrain) == 0x3C, "SRayHit::isTerrain offset mismatch");

    /**
     * Concrete CryPhysics world implementation.
     *
     * RTTI places IPhysicalWorld at +0x00, IPhysUtils at +0x08, and the
     * CGeomManager/IGeomManager base at +0x10. The primary table at
     * 0x18407FC80 implements all 94 IPhysicalWorld entries and appends two
     * concrete operations. Matching allocation and scalar deletion prove the
     * complete 0x60E8-byte size.
     */
    class CPhysicalWorld : public IPhysicalWorld,
                           public IPhysUtils,
                           public CGeomManager {
      public:
        /** Everything solid a ground query should stand on. */
        static constexpr int kGroundObjectTypes = ent_static | ent_sleeping_rigid | ent_rigid | ent_terrain;

        /**
         * The flags the game's own ground queries use. The high half is
         * `geom_colltype1 << rwi_colltype_bit`, so the ray sees only geometry
         * a player can stand on -- right for a probe, wrong for a pick.
         */
        static constexpr unsigned int kGroundRayFlags = rwi_colltype_all(geom_colltype_player) | rwi_stop_at_pierceable;

        /** Everything a ray can hit. */
        static constexpr int kAllObjectTypes = ent_all;

        /**
         * "What is under this ray", from the game's own hitscan:
         * C_ShootingSystem::FireWeapon traces with objtypes 287, flags 1039.
         *
         * 1039 is `rwi_colltype_any | rwi_stop_at_pierceable`. Without the
         * first the query requires geom_colltype_ray, and with a colltype
         * shifted in it requires that one -- either way the ray passes
         * through most of the level and reports what lies behind it.
         */
        static constexpr unsigned int kPickRayFlags = rwi_colltype_any | rwi_stop_at_pierceable;

        /**
         * World visibility probe types: `ent_static | ent_sleeping_rigid |
         * ent_rigid | ent_terrain`. Doors and physicalized props still
         * block, while actors cannot -- a living entity is sim class 3 and
         * an animated skeleton is an articulated entity at sim class 4, so
         * neither is reachable through these types.
         *
         * `CScriptBindPhysics::RayTraceCheck` (0x1839AA14C) traces 257,
         * which also excludes the rigid classes.
         */
        static constexpr int kVisibilityObjectTypes = kGroundObjectTypes;

        /**
         * The matching flags: `rwi_pierceability(13) | rwi_ignore_noncolliding
         * | rwi_ignore_solid_back_faces`. With no collision type shifted in,
         * the ray keeps `geom_colltype_ray`, and `rwi_ignore_noncolliding` adds
         * `geom_colltype0`, so it sees world geometry that actually collides
         * and nothing else.
         *
         * Pierceability 13 lets glass, foliage and other thin materials
         * pass. At `nMaxHits` 1 a pierceable contact is dropped rather than
         * recorded, so the hit count still means "something solid".
         *
         * `rwi_any_hit` deliberately absent. It sets
         * `intersection_params::bStopAtFirstTri`, and a mesh that stops on a
         * pierceable triangle never offers its solid ones -- a window would
         * clear the whole wall it is set in. It is only safe at pierceability
         * 15, where nothing is dropped, which is why no native probe pairs the
         * two.
         *
         * `rwi_ignore_solid_back_faces` costs nothing on a normal segment,
         * whose entry face is nearer, and stops a camera clipped inside
         * geometry from reporting everything as occluded.
         */
        static constexpr unsigned int kVisibilityRayFlags = rwi_pierceability(13) | rwi_ignore_noncolliding | rwi_ignore_solid_back_faces;

        // The numbers the game's own traces pass, kept so that spelling them
        // as flags cannot quietly change what is asked for.
        static_assert(kGroundObjectTypes == 0x107, "ground objtypes mismatch");
        static_assert(kGroundRayFlags == 0x2000F, "ground ray flags mismatch");
        static_assert(kAllObjectTypes == 287, "ent_all mismatch");
        static_assert(kPickRayFlags == 1039, "pick ray flags mismatch");
        static_assert(kVisibilityRayFlags == 0x14D, "visibility ray flags mismatch");

        /**
         * Traces from `origin` to `origin + direction`; returns the hit count.
         * `skip` names colliders to pass through, which is how the game keeps
         * a shot from hitting the actor that fired it.
         */
        int RayWorldIntersection(const Vec3 &origin, const Vec3 &direction, int objectTypes, unsigned int flags, SRayHit *hits, int maxHits, IPhysicalEntity **skip = nullptr, int skipCount = 0);

        /**
         * The ground under `position`, probing from `probeUp` above it to
         * `probeDown` below, so a point already slightly underground still
         * resolves. False when the probe finds nothing.
         *
         * `skip` names colliders the probe passes through. A probe over a
         * body standing at `position` has to name both of that body's: the
         * entity's and the animated skeleton's are separate, and skipping
         * only the first still stops the ray on the second.
         */
        bool ResolveGround(const Vec3 &position, float probeUp, float probeDown, Vec3 &ground, IPhysicalEntity **skip = nullptr, int skipCount = 0);

        /**
         * Concrete iterator returned by the physical world.
         *
         * RTTI preserves this exact nested class identity. Its table at
         * 0x1847A3D20 contains exactly the seven IPhysicalEntityIt entries,
         * while allocation and scalar deletion prove the 0x20-byte layout.
         */
        class CPhysicalEntityIt : public IPhysicalEntityIt {
          public:
            ~CPhysicalEntityIt() override = default;

          private:
            CPhysicalWorld *m_world; // +0x08
            std::byte m_opaque0010[0x10];
        };

        ~CPhysicalWorld() override = default;

        static CPhysicalWorld *GetInstance();

        virtual void unk_fn0094() = 0;
        virtual void unk_fn0095() = 0; // primary +0x2F8

      private:
        PhysicsVars m_vars;          // 0x68
        void *m_pLog;                // 0x248, ILog, the constructor's argument
        std::byte m_opaque0250[0x5E98];
    };

    static_assert(sizeof(CPhysicalWorld) == 0x60E8,
                  "CPhysicalWorld size mismatch");
    static_assert(sizeof(CPhysicalWorld::CPhysicalEntityIt) == 0x20,
                  "CPhysicalWorld::CPhysicalEntityIt size mismatch");
} // namespace SDK
