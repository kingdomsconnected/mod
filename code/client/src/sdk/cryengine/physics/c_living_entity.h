#pragma once

#include "c_physical_entity.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryPhysics living-character entity, vtable 0x183A82570.
     *
     * RTTI proves direct CPhysicalEntity inheritance at +0x00. Its 75-entry
     * table has the same boundary as CPhysicalEntity and therefore adds no
     * primary virtual operations. Matching allocation and sized-deletion
     * paths prove the 0x360-byte object size.
     */
    class CLivingEntity : public CPhysicalEntity {
      public:
        ~CLivingEntity() override = default;

      protected:
        // CLivingEntity constructor 0x180BCE4A8 writes these defaults: gravity
        // -9.81, kInertia 8, kAirControl 0.1, hCyl 1.1, hEye 1.7, size
        // (0.3, 0.4, 0.6), nodSpeed 60, mass 80 and 1/40, three ground ids of
        // -1, the slope cosines, maxVelGround 10, timeImpulseRecover 5, hHead
        // 1.9, collTypes 271, timeSmooth 0.16 and the identity deltaQRot.
        // Gaps are KCD2's own additions.
        std::uint32_t m_state0158;       // 0x158
        SDK::Vec3 m_vel;                 // 0x15C
        SDK::Vec3 m_velRequested;        // 0x168
        SDK::Vec3 m_gravity;             // 0x174, the world's gravity
        SDK::Vec3 m_nslope;              // 0x180, (0, 0, 1)
        SDK::Vec3 m_gravity018C;         // 0x18C, a second copy of the world's gravity
        float m_dtRequested;             // 0x198
        float m_kInertia;                // 0x19C
        float m_kInertiaAccel;           // 0x1A0
        float m_kAirControl;             // 0x1A4
        float m_kAirResistance;          // 0x1A8
        float m_hCyl;                    // 0x1AC
        float m_hEye;                    // 0x1B0
        float m_hPivot;                  // 0x1B4
        SDK::Vec3 m_size;                // 0x1B8
        float m_dh;                      // 0x1C4
        float m_dhSpeed;                 // 0x1C8
        float m_dhAcc;                   // 0x1CC
        float m_stablehTime;             // 0x1D0
        float m_hLatest;                 // 0x1D4
        float m_nodSpeed;                // 0x1D8
        float m_mass;                    // 0x1DC
        float m_massinv;                 // 0x1E0
        std::int32_t m_surface_idx;      // 0x1E4
        std::int32_t m_lastGroundSurfaceIdx;    // 0x1E8
        std::int32_t m_lastGroundSurfaceIdxAux; // 0x1EC
        std::int32_t m_lastGroundPrim;   // 0x1F0
        float m_timeFlying;              // 0x1F4
        float m_timeForceInertia;        // 0x1F8
        std::byte m_state01FC[0x14];     // 0x1FC
        float m_slopeSlide;              // 0x210
        float m_slopeClimb;              // 0x214
        float m_slopeJump;               // 0x218
        float m_slopeFall;               // 0x21C
        float m_maxVelGround;            // 0x220
        float m_groundContactEps;        // 0x224
        float m_timeImpulseRecover;      // 0x228
        std::uint32_t m_padding022C;
        void *m_pCylinderGeom;           // 0x230, CCylinderGeom or CCapsuleGeom
        float m_hHead;                   // 0x238
        std::uint32_t m_padding023C;
        void *m_pHeadGeom;               // 0x240
        std::byte m_CylinderGeomPhys[0x50]; // 0x248, phys_geometry; its nRefCount (-1) at +0x34
        std::byte m_state0298[0x8];      // 0x298
        float m_timeUseLowCap;           // 0x2A0
        float m_timeSinceStanceChange;   // 0x2A4
        float m_timeSinceImpulseContact; // 0x2A8
        float m_timeStepFull;            // 0x2AC
        float m_timeStepPerformed;       // 0x2B0
        std::int32_t m_iSnapshot;        // 0x2B4
        std::int32_t m_iTimeLastSend;    // 0x2B8
        std::int32_t m_collTypes;        // 0x2BC
        void *m_pLivingEntToIgnore;      // 0x2C0
        std::uint32_t m_livingFlags;     // 0x2C8, m_bFlying, m_bJumpRequested, m_bSwimming, m_bUseCapsule and the rest
        std::uint8_t m_state02CC;        // 0x2CC
        std::byte m_padding02CD[0x7];
        std::int32_t m_state02D4;        // 0x2D4, -1 by default
        void *m_pLastGroundCollider;     // 0x2D8
        std::int32_t m_iLastGroundColliderPart; // 0x2E0
        SDK::Vec3 m_posLastGroundColl;   // 0x2E4
        SDK::Vec3 m_velGround;           // 0x2F0
        SDK::Vec3 m_deltaPos;            // 0x2FC
        SDK::Vec3 m_posLocal;            // 0x308
        SDK::Vec3 m_deltaV;              // 0x314
        SDK::Quat m_deltaQRot;           // 0x320
        float m_timeSmooth;              // 0x330
        float m_timeRotChanged;          // 0x334
        void *m_pContacts;               // 0x338
        std::int32_t m_nContacts;        // 0x340
        std::int32_t m_nContactsAlloc;   // 0x344
        void *m_pBody;                   // 0x348
        std::int32_t m_lockLiving;       // 0x350
        std::int32_t m_lockStep;         // 0x354
        std::byte m_state0358[0x8];      // 0x358

      private:
        static void AssertLayout() {
            static_assert(offsetof(CLivingEntity, m_gravity) == 0x174, "CLivingEntity::m_gravity offset mismatch");
            static_assert(offsetof(CLivingEntity, m_kInertia) == 0x19C, "CLivingEntity::m_kInertia offset mismatch");
            static_assert(offsetof(CLivingEntity, m_size) == 0x1B8, "CLivingEntity::m_size offset mismatch");
            static_assert(offsetof(CLivingEntity, m_mass) == 0x1DC, "CLivingEntity::m_mass offset mismatch");
            static_assert(offsetof(CLivingEntity, m_slopeSlide) == 0x210, "CLivingEntity::m_slopeSlide offset mismatch");
            static_assert(offsetof(CLivingEntity, m_pCylinderGeom) == 0x230, "CLivingEntity::m_pCylinderGeom offset mismatch");
            static_assert(offsetof(CLivingEntity, m_CylinderGeomPhys) == 0x248, "CLivingEntity::m_CylinderGeomPhys offset mismatch");
            static_assert(offsetof(CLivingEntity, m_collTypes) == 0x2BC, "CLivingEntity::m_collTypes offset mismatch");
            static_assert(offsetof(CLivingEntity, m_pLastGroundCollider) == 0x2D8, "CLivingEntity::m_pLastGroundCollider offset mismatch");
            static_assert(offsetof(CLivingEntity, m_deltaQRot) == 0x320, "CLivingEntity::m_deltaQRot offset mismatch");
            static_assert(offsetof(CLivingEntity, m_pBody) == 0x348, "CLivingEntity::m_pBody offset mismatch");
        }
    };

    static_assert(sizeof(CLivingEntity) == 0x360,
                  "CLivingEntity size mismatch");
} // namespace SDK
