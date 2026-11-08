#pragma once

#include "c_physical_entity.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryPhysics rigid-body entity, vtable 0x183A48DC0.
     *
     * RTTI places CPhysicalEntity at +0x00. The 81-entry table implements all
     * 75 inherited entries and appends six rigid-body operations. Matching
     * allocation and sized-deletion paths prove the 0x438-byte object size.
     */
    class CRigidEntity : public CPhysicalEntity {
      public:
        ~CRigidEntity() override = default;

        virtual void unk_fn0075() = 0;
        virtual void unk_fn0076() = 0;
        virtual void unk_fn0077() = 0;
        virtual void unk_fn0078() = 0;
        virtual void unk_fn0079() = 0;
        virtual void unk_fn0080() = 0; // +0x280

      protected:
        // CRigidEntity constructor 0x1806EC954 points m_pNewCoords at
        // m_posNew and sets the identity m_qNew, m_iVarPart0 1000000, the
        // contact list head to itself, collTypes 287, timeStepFull 0.01,
        // the world's gravity twice, Emin 0.07 squared, maxAllowedStep 0.02,
        // maxw 20, friction 0.1 to 100, the two -2 log indices around
        // vcollMin 1e10, water density and resistance 1, EminWater 0.01
        // squared and m_iLastConstraintIdx 1.
        SDK::Vec3 m_posNew;              // 0x158
        SDK::Quat m_qNew;                // 0x164
        SDK::Vec3 m_vectors0174[2];      // 0x174
        std::int32_t m_iVarPart0;        // 0x18C
        std::uint32_t m_rigidFlags;      // 0x190, m_bCollisionCulling, m_bJustLoaded, m_bStable and the rest
        std::uint32_t m_rigidFlags2;     // 0x194, low byte m_bAwake
        void *m_pColliderContacts;       // 0x198
        void *m_pColliderConstraints;    // 0x1A0
        void *m_pContactStart;           // 0x1A8, entity_contact list head, pointing at itself when empty
        void *m_pContactEnd;             // 0x1B0
        std::int32_t m_nContacts;        // 0x1B8
        std::uint32_t m_padding01BC;
        void *m_pConstraints;            // 0x1C0
        void *m_pConstraintInfos;        // 0x1C8
        std::int32_t m_nConstraintsAlloc; // 0x1D0
        std::uint32_t m_padding01D4;
        std::uint64_t m_constraintMask;  // 0x1D8
        std::uint64_t m_state01E0;       // 0x1E0
        std::int32_t m_collTypes;        // 0x1E8
        float m_velFastDir;              // 0x1EC
        float m_sizeFastDir;             // 0x1F0
        float m_timeStepFull;            // 0x1F4
        float m_timeStepPerformed;       // 0x1F8
        float m_lastTimeStep;            // 0x1FC
        float m_minAwakeTime;            // 0x200
        float m_nextTimeStep;            // 0x204
        SDK::Vec3 m_gravity;             // 0x208
        SDK::Vec3 m_gravityFreefall;     // 0x214
        float m_Emin;                    // 0x220
        float m_maxAllowedStep;          // 0x224
        SDK::Vec3 m_vAccum;              // 0x228
        SDK::Vec3 m_wAccum;              // 0x234
        float m_damping;                 // 0x240
        float m_dampingFreefall;         // 0x244
        float m_dampingEx;               // 0x248
        float m_maxw;                    // 0x24C
        float m_minFriction;             // 0x250
        float m_maxFriction;             // 0x254
        SDK::Vec3 m_vSleep;              // 0x258
        SDK::Vec3 m_wSleep;              // 0x264
        void *m_pStableContact;          // 0x270
        // Built by 0x1806ECDD4, then the momentum accumulators down to the
        // canopy state; KCD2's packing of them is not pinned.
        std::byte m_state0278[0x12C];    // 0x278
        float m_timeCanopyFallen;        // 0x3A4
        std::uint32_t m_canopyState;     // 0x3A8, m_bCanopyContact in the low byte, m_nCanopyContactsLost above
        SDK::Vec3 m_Psoft;               // 0x3AC
        SDK::Vec3 m_Lsoft;               // 0x3B8
        SDK::Vec3 m_forcedMove;          // 0x3C4
        void *m_pEventsColl;             // 0x3D0
        std::int32_t m_iLastLogColl;     // 0x3D8
        float m_vcollMin;                // 0x3DC
        std::int32_t m_iLastLog;         // 0x3E0
        std::uint32_t m_padding03E4;
        void *m_pEvent;                  // 0x3E8
        float m_waterDamping;            // 0x3F0
        float m_kwaterDensity;           // 0x3F4
        float m_kwaterResistance;        // 0x3F8
        float m_EminWater;               // 0x3FC
        float m_submergedFraction;       // 0x400
        std::int32_t m_iLastConstraintIdx; // 0x404
        std::int32_t m_lockConstraintIdx; // 0x408
        std::int32_t m_lockContacts;     // 0x40C
        std::int32_t m_lockStep;         // 0x410
        std::int32_t m_iLastChecksum;    // 0x414
        std::byte m_state0418[0x20];     // 0x418

      private:
        static void AssertLayout() {
            static_assert(offsetof(CRigidEntity, m_iVarPart0) == 0x18C, "CRigidEntity::m_iVarPart0 offset mismatch");
            static_assert(offsetof(CRigidEntity, m_pContactStart) == 0x1A8, "CRigidEntity::m_pContactStart offset mismatch");
            static_assert(offsetof(CRigidEntity, m_collTypes) == 0x1E8, "CRigidEntity::m_collTypes offset mismatch");
            static_assert(offsetof(CRigidEntity, m_gravity) == 0x208, "CRigidEntity::m_gravity offset mismatch");
            static_assert(offsetof(CRigidEntity, m_maxw) == 0x24C, "CRigidEntity::m_maxw offset mismatch");
            static_assert(offsetof(CRigidEntity, m_pStableContact) == 0x270, "CRigidEntity::m_pStableContact offset mismatch");
            static_assert(offsetof(CRigidEntity, m_timeCanopyFallen) == 0x3A4, "CRigidEntity::m_timeCanopyFallen offset mismatch");
            static_assert(offsetof(CRigidEntity, m_pEvent) == 0x3E8, "CRigidEntity::m_pEvent offset mismatch");
            static_assert(offsetof(CRigidEntity, m_iLastChecksum) == 0x414, "CRigidEntity::m_iLastChecksum offset mismatch");
        }
    };

    static_assert(sizeof(CRigidEntity) == 0x438,
                  "CRigidEntity size mismatch");
} // namespace SDK
