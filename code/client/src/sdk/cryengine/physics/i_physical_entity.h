#pragma once

#include "pe_geomparams.h"
#include "pe_params.h"
#include "phys_geometry.h"

#include <cstdint>

namespace SDK {
    /**
     * Which query a `GetStatus` structure is: the type id every `pe_status_*`
     * carries in its first four bytes, and the only thing that tells the
     * physics entity which branch to take.
     *
     * CryPhysics' stock `EPE_Status` numbering. `ePE_status_living` is the
     * one KCD2 proves directly -- `CLivingEntity::GetStatus` (0x1808A58A0)
     * fills a walking body's status under its type-2 branch -- and the rest
     * are the engine's own values, listed so a new query does not have to
     * rediscover the ordering.
     */
    enum EPE_Status : std::int32_t {
        ePE_status_pos                 = 1,
        ePE_status_living              = 2,
        ePE_status_vehicle             = 4,
        ePE_status_wheel               = 5,
        ePE_status_joint               = 6,
        ePE_status_awake               = 7,
        ePE_status_dynamics            = 8,
        ePE_status_collisions          = 9,
        ePE_status_id                  = 10,
        ePE_status_timeslices          = 11,
        ePE_status_nparts              = 12,
        ePE_status_contains_point      = 13,
        ePE_status_rope                = 14,
        ePE_status_vehicle_abilities   = 15,
        ePE_status_placeholder         = 16,
        ePE_status_softvtx             = 17,
        ePE_status_sensors             = 18,
        ePE_status_sample_contact_area = 19,
        ePE_status_caps                = 20,
        ePE_status_check_stance        = 21,
        ePE_status_waterman            = 22,
        ePE_status_area                = 23,
        ePE_status_extent              = 24,
        ePE_status_random              = 25,
        ePE_status_constraint          = 26,
        ePE_status_netpos              = 27,
    };

    /** GetStatus returns the part count directly; no output fields. */
    struct pe_status_nparts {
        std::int32_t type = ePE_status_nparts;
    };

    static_assert(sizeof(pe_status_nparts) == 4);

    /**
     * What kind of body a physical entity is, as `IPhysicalEntity::GetType`
     * answers. CryPhysics' stock `pe_type`; `PE_PARTICLE` is the one KCD2
     * proves, through `CProjectile::SetVelocity`'s check for 5.
     */
    enum pe_type : int {
        PE_NONE            = 0,
        PE_STATIC          = 1,
        PE_RIGID           = 2,
        PE_WHEELEDVEHICLE  = 3,
        PE_LIVING          = 4,
        PE_PARTICLE        = 5,
        PE_ARTICULATED     = 6,
        PE_ROPE            = 7,
        PE_SOFT            = 8,
        PE_AREA            = 9,
        PE_GRID            = 10,
        PE_WALKINGRIGID    = 11,
    };

    /**
     * What a collider's foreign pointer points at. CryEngine's stock
     * EPhysicsForeignIds; KCD2 keeps the numbering, which CBrush's
     * physicalize path proves by passing id 1 and itself.
     */
    enum EPhysicsForeignId : int {
        PHYS_FOREIGN_ID_TERRAIN = 0,
        PHYS_FOREIGN_ID_STATIC  = 1, // IRenderNode *
        PHYS_FOREIGN_ID_ENTITY  = 2, // IEntity *
        PHYS_FOREIGN_ID_FOLIAGE = 3,
        PHYS_FOREIGN_ID_ROPE    = 4,
    };

    // KCD2 CryPhysics interface, vtable 0x183AB6A30 with 28 slots.
    struct IPhysicalEntity {
        virtual ~IPhysicalEntity()                                   = default; // +0x00
        virtual pe_type GetType() const                              = 0;       // +0x08; CProjectile::SetVelocity checks PE_PARTICLE.
        virtual void unk_fn0002()                                    = 0;
        virtual void unk_fn0003()                                    = 0;
        virtual int SetParams(pe_params *params, int threadSafe = 1) = 0; // +0x20; CProjectile::Launch and SetVelocity.
        virtual int GetParams(pe_params *params) const                = 0;       // +0x28; the counterpart of SetParams, read by CArrow::ProcessCollision.

        // Returns zero for unsupported queries; status starts with a type ID.
        virtual int GetStatus(void *status) const = 0; // +0x30
        // An action struct starts with its type id, as a params struct does;
        // CScriptBind_Entity::SetVelocityEx (0x1837DF5D8) calls it with threadSafe 0.
        virtual int Action(const void *action, int threadSafe = 0) = 0; // +0x38
        // 0x18084B2D4: returns a part id (zero is success), or -1; retains geometry even when insertion is queued.
        virtual int AddGeometry(phys_geometry *geometry, pe_geomparams *params, int id = -1, int threadSafe = 0) = 0; // +0x40
        virtual void unk_fn0009()                 = 0;
        /**
         * The owner this collider was physicalized for, or null when its
         * foreign id is not `foreignDataType`. Read GetiForeignData() first:
         * every other id comes back null, which is indistinguishable from
         * having no owner.
         */
        virtual void *GetForeignData(int foreignDataType = 0) const = 0; // +0x50
        virtual int GetiForeignData() const                        = 0; // +0x58
        virtual void unk_fn0012()                 = 0;
        virtual void unk_fn0013()                 = 0;
        virtual void unk_fn0014()                 = 0;
        virtual void unk_fn0015()                 = 0;
        virtual void unk_fn0016()                 = 0;
        virtual void unk_fn0017()                 = 0;
        virtual void unk_fn0018()                 = 0;
        virtual void unk_fn0019()                 = 0;
        virtual void unk_fn0020()                 = 0;
        virtual void unk_fn0021()                 = 0;
        virtual void unk_fn0022()                 = 0;
        virtual void unk_fn0023()                 = 0;
        virtual void unk_fn0024()                 = 0;
        virtual void unk_fn0025()                 = 0;
        virtual void unk_fn0026()                 = 0;
        virtual void unk_fn0027()                 = 0; // +0xD8
    };

    static_assert(sizeof(IPhysicalEntity) == 0x8, "IPhysicalEntity size mismatch");
} // namespace SDK
