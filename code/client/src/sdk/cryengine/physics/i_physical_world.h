#pragma once

#include "../../types.h"
#include "i_geom_manager.h"
#include "i_physical_entity.h"

#include <cstdint>

namespace SDK {
    /**
     * Which simulation classes a world query walks, the `objtypes` argument
     * of `RayWorldIntersection` and `GetEntitiesInBox`.
     *
     * CryPhysics' stock `entity_query_flags`; KCD2 keeps the numbering, which
     * its own queries prove: `C_ShootingSystem::FireWeapon` and
     * `C_CameraDebug::SetMode` both trace 287, which is exactly `ent_all`,
     * and `CScriptBindPhysics::RayTraceCheck` (0x1839AA14C) traces 257, which
     * is `ent_static | ent_terrain`. The classes map onto the sim classes an
     * entity is registered under, which is why no combination of them reaches
     * an actor: a living entity is sim class 3 and an animated skeleton an
     * articulated entity at sim class 4.
     */
    enum entity_query_flags : std::int32_t {
        ent_static              = 0x0001,
        ent_sleeping_rigid      = 0x0002,
        ent_rigid               = 0x0004,
        ent_living              = 0x0008,
        ent_independent         = 0x0010,
        ent_areas               = 0x0020,
        ent_triggers            = 0x0040,
        ent_deleted             = 0x0080,
        ent_terrain             = 0x0100,
        /** Ray queries only. */
        ent_water               = 0x0200,
        ent_all                 = ent_static | ent_sleeping_rigid | ent_rigid | ent_living | ent_independent | ent_terrain,
        ent_ignore_noncolliding = 0x10000,
        ent_sort_by_mass        = 0x20000,
        ent_allocate_list       = 0x40000,
    };

    /**
     * How a ray behaves once it is tracing, the `flags` argument of
     * `RayWorldIntersection`.
     *
     * CryPhysics' stock `rwi_flags`, again pinned by the game's own traces:
     * `FireWeapon` passes 1039 (`rwi_colltype_any | rwi_stop_at_pierceable`),
     * `C_CameraDebug::SetMode` passes 47 (`rwi_ignore_terrain_holes |
     * rwi_stop_at_pierceable`), and the ground probe passes 0x2000F, whose
     * high half is a collision type shifted in by `rwi_colltype_bit`.
     *
     * The low four bits are the ray's pierceability rather than a flag: a hit
     * is solid when the ray's pierceability is at least the surface's, so
     * `rwi_stop_at_pierceable` (15) stops at everything and a lower number
     * passes through the thin materials above it.
     */
    enum rwi_flags : std::uint32_t {
        rwi_pierceability_mask       = 0x000F,
        rwi_pierceability0           = 0x0000,
        rwi_stop_at_pierceable       = 0x000F,
        rwi_max_piercing             = 0x0010,
        rwi_ignore_terrain_holes     = 0x0020,
        rwi_ignore_noncolliding      = 0x0040,
        rwi_ignore_back_faces        = 0x0080,
        rwi_ignore_solid_back_faces  = 0x0100,
        rwi_colltype_any             = 0x0400,
        rwi_queue                    = 0x0800,
        rwi_force_pierceable_noncoll = 0x1000,
        rwi_update_last_hit          = 0x4000,
        rwi_any_hit                  = 0x8000,
        /** Where a `geom_flags` collision type sits inside the ray flags. */
        rwi_colltype_bit             = 16,
    };

    /**
     * Geometry collision types, the part flags a ray can be told to require.
     *
     * A ray with no type shifted in keeps CryPhysics' default
     * `geom_colltype_ray`; `rwi_ignore_noncolliding` adds `geom_colltype0`.
     */
    enum geom_flags : std::uint32_t {
        geom_colltype0          = 0x0001,
        geom_colltype1          = 0x0002,
        geom_colltype2          = 0x0004,
        geom_colltype3          = 0x0008,
        geom_colltype4          = 0x0010,
        geom_colltype5          = 0x0020,
        geom_colltype_ray       = 0x8000,

        geom_colltype_player    = geom_colltype1,
        geom_colltype_explosion = geom_colltype2,
        geom_colltype_vehicle   = geom_colltype3,
        geom_colltype_foliage   = geom_colltype4,
        geom_colltype_debris    = geom_colltype5,
    };

    /**
     * What a surface index means, as `GetSurfaceParameters` reports it.
     *
     * The low four bits are the surface's pierceability on the same scale as
     * a ray's: a contact is solid when the ray -- or the projectile -- is at
     * least as pierceable as the surface, so 15 is a surface everything goes
     * straight through and 0 is one nothing does. CryPhysics starts its whole
     * table at `sf_max_pierceable`, which is why geometry that was never
     * given a surface type stops nothing at all.
     */
    enum surface_flags : std::uint32_t {
        sf_pierceable_mask     = 0x000F,
        sf_max_pierceable      = 0x000F,
        sf_important           = 0x0200,
        sf_manually_breakable  = 0x0400,
        sf_matbreakable_bit    = 16,
    };

    /** What `IPhysicalWorld::DestroyPhysicalEntity` does with the entity. CryPhysics' stock numbering. */
    enum EDestroyMode : int {
        DM_NORMAL             = 0,
        DM_SUSPEND            = 1,
        DM_RESTORE            = 2,
        DM_KEEP_IF_REFERENCED = 4,
    };

    /** The ray's pierceability, which occupies the low four flag bits. */
    constexpr std::uint32_t rwi_pierceability(std::uint32_t pierceability) {
        return pierceability & rwi_pierceability_mask;
    }

    /** Requires every one of `collisionTypes` of the geometry the ray hits. */
    constexpr std::uint32_t rwi_colltype_all(std::uint32_t collisionTypes) {
        return collisionTypes << rwi_colltype_bit;
    }

    /**
     * Abstract CryPhysics world interface, vtable 0x18407F950.
     *
     * KCD2 emits exactly 94 entries. Named contracts below are independently
     * proven against its concrete CPhysicalWorld table at 0x18407FC80.
     */
    class IPhysicalWorld {
      public:
        virtual ~IPhysicalWorld() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual IGeomManager *GetGeomManager() = 0; // +0x20, 0x18084BF50; adjusts to the geometry-manager base
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        // 0x1808463B8 forwards to the lifetime overload with lifetime zero and no placeholder. heap is an opaque IGeneralMemoryHeap*.
        virtual IPhysicalEntity *CreatePhysicalEntity(pe_type type, pe_params *params = nullptr, void *foreignData = nullptr, int foreignDataId = 0, int id = -1, void *heap = nullptr) = 0; // +0x88
        virtual void unk_fn0018() = 0;
        /**
         * Destroys, suspends or restores a physical entity; `mode` is an
         * `EDestroyMode`. Suspending takes the entity out of the world onto
         * the hidden list and restoring puts it back, both idempotent, and a
         * suspended entity is still destroyed normally by its owner later.
         *
         * `CPhysicalWorld::DestroyPhysicalEntity` (0x180846E10): slot 19 in
         * CryPhysics' declaration order, queued as opcode 5 while the world
         * steps, the hidden list at +0x2F0. Called with mode 0 by
         * `C_PhysicalizationManager::DeactivateLayer` (0x180A60470).
         */
        virtual int DestroyPhysicalEntity(IPhysicalEntity *entity, int mode = 0, int threadSafe = 0) = 0; // +0x98
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        /**
         * The physical entity with this id, or null. -1 is the terrain's own
         * body and -2 the world's. `CPhysicalWorld` 0x180667418; the combat
         * module's contact listener (0x180D51854) resolves a contact's
         * `geom_contact` entity id through it.
         */
        virtual IPhysicalEntity *GetPhysicalEntityById(int id) = 0; // +0xB0
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        /**
         * The bounciness, friction and `surface_flags` of one surface index.
         *
         * `CArrow::ProcessCollision` (0x182ACBFA8) calls this through
         * 0x182AA0384 with `EventPhysCollision::idmat[1]`, masks the flags
         * with `sf_pierceable_mask` and abandons the collision outright when
         * the result exceeds the projectile's own `iPierceability`. That is
         * the one exit in the whole function that leaves the projectile
         * flying, so a body whose parts carry an unmapped surface -- which
         * reads back as maximally pierceable -- is one nothing can hit.
         */
        virtual void GetSurfaceParameters(int surfaceIndex, float &bounciness, float &friction, std::uint32_t &flags) const = 0; // +0xD0
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        /**
         * Every physical entity whose box overlaps `ptMin`..`ptMax` and whose
         * class `objectTypes` (`entity_query_flags`) admits. `list` is left
         * pointing at the world's own per-thread buffer -- valid until the
         * next query, never freed by the caller -- unless `ent_allocate_list`
         * is passed. Returns the count. `CPhysicalWorld::GetEntitiesInBox`
         * (0x18084C354) takes the world's write lock and forwards to
         * `GetEntitiesAround`; slot 34, the one before RayWorldIntersection.
         */
        virtual int GetEntitiesInBox(Vec3 ptMin, Vec3 ptMax, IPhysicalEntity **&list, int objectTypes, int listPrealloc = 0) = 0; // +0x110
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        virtual void unk_fn0044() = 0;
        virtual void unk_fn0045() = 0;
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0;
        virtual void unk_fn0052() = 0;
        virtual void unk_fn0053() = 0;
        virtual void unk_fn0054() = 0;
        virtual void unk_fn0055() = 0;
        virtual void unk_fn0056() = 0;
        virtual void unk_fn0057() = 0;
        virtual void unk_fn0058() = 0;
        virtual void unk_fn0059() = 0;
        virtual void unk_fn0060() = 0;
        virtual void unk_fn0061() = 0;
        virtual void unk_fn0062() = 0;
        virtual void unk_fn0063() = 0;
        virtual void unk_fn0064() = 0;
        virtual void unk_fn0065() = 0;
        virtual void unk_fn0066() = 0;
        virtual void unk_fn0067() = 0;
        virtual void unk_fn0068() = 0;
        virtual void unk_fn0069() = 0;
        virtual void unk_fn0070() = 0;
        virtual void unk_fn0071() = 0;
        virtual void unk_fn0072() = 0;
        virtual void unk_fn0073() = 0;
        virtual void unk_fn0074() = 0;
        virtual void unk_fn0075() = 0;
        virtual void unk_fn0076() = 0;
        virtual void unk_fn0077() = 0;
        virtual void unk_fn0078() = 0;
        virtual void unk_fn0079() = 0;
        virtual void unk_fn0080() = 0;
        virtual void unk_fn0081() = 0;
        virtual void unk_fn0082() = 0;
        virtual void unk_fn0083() = 0;
        virtual void unk_fn0084() = 0;
        virtual void unk_fn0085() = 0;
        virtual void unk_fn0086() = 0;
        virtual void unk_fn0087() = 0;
        virtual void unk_fn0088() = 0;
        virtual void unk_fn0089() = 0;
        virtual void unk_fn0090() = 0;
        virtual void unk_fn0091() = 0;
        virtual void unk_fn0092() = 0;
        virtual void unk_fn0093() = 0; // +0x2E8
    };

    static_assert(sizeof(IPhysicalWorld) == 0x8, "IPhysicalWorld size mismatch");
} // namespace SDK
