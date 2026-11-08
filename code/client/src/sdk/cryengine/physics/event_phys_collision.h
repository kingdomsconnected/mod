#pragma once

#include "sdk/types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class IPhysicalEntity;

    /**
     * One contact, as CryPhysics reports it to a projectile.
     *
     * CryPhysics builds this out of `EventPhys` (the queue link and the
     * event's type id) and `EventPhysStereo` (the two bodies), which are
     * flattened here because nothing else in the SDK needs them apart.
     *
     * Index 0 is the body that caused the event -- for an arrow, the arrow --
     * and index 1 is what it ran into; every array below is indexed the same
     * way. `CArrow::ProcessCollision` (0x182ACBFA8) reads exactly this
     * layout: `pt` at +0x3C as the `Vec3` it hands `SpawnRecoverableItem`,
     * `vloc[i]` at +0x54 with a 12-byte stride, `partid[1]` at +0x78 as the
     * part it attaches an arrow to, and `idmat[i]` as an `int16` pair at
     * +0x7C.
     *
     * `idmat[1]` is the one that decides whether a hit happens at all: the
     * game looks its surface up and abandons the whole collision when that
     * surface is more pierceable than the projectile. See
     * `IPhysicalWorld::GetSurfaceParameters`.
     *
     * KCD2 appends fields of its own after CryPhysics' `deltaT`, so the tail
     * is opaque and the size is not asserted -- the structure is never
     * constructed here, it only ever arrives by reference from the engine.
     */
    struct EventPhysCollision {
        void *next;                   // +0x00  EventPhys::next
        std::int32_t idval;           // +0x08
        IPhysicalEntity *pEntity[2];  // +0x10  EventPhysStereo
        void *pForeignData[2];        // +0x20
        std::int32_t iForeignData[2]; // +0x30
        std::int32_t idCollider;      // +0x38
        Vec3 pt;                      // +0x3C
        Vec3 n;                       // +0x48
        Vec3 vloc[2];                 // +0x54
        float mass[2];                // +0x6C
        std::int32_t partid[2];       // +0x74
        std::int16_t idmat[2];        // +0x7C
        std::int16_t iPrim[2];        // +0x80
        float penetration;            // +0x84
        float normImpulse;            // +0x88
        float radius;                 // +0x8C
        void *pEntContact;            // +0x90
        char deltaT;                  // +0x98
    };

    static_assert(offsetof(EventPhysCollision, pEntity) == 0x10, "EventPhysCollision::pEntity offset mismatch");
    static_assert(offsetof(EventPhysCollision, iForeignData) == 0x30, "EventPhysCollision::iForeignData offset mismatch");
    static_assert(offsetof(EventPhysCollision, pt) == 0x3C, "EventPhysCollision::pt offset mismatch");
    static_assert(offsetof(EventPhysCollision, n) == 0x48, "EventPhysCollision::n offset mismatch");
    static_assert(offsetof(EventPhysCollision, vloc) == 0x54, "EventPhysCollision::vloc offset mismatch");
    static_assert(offsetof(EventPhysCollision, partid) == 0x74, "EventPhysCollision::partid offset mismatch");
    static_assert(offsetof(EventPhysCollision, idmat) == 0x7C, "EventPhysCollision::idmat offset mismatch");
} // namespace SDK
