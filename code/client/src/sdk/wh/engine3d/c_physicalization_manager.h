#pragma once

#include "../../cryengine/3dengine/cry_3d_engine_base.h"
#include "../../cryengine/physics/i_physical_entity.h"
#include "../../cryengine/system/i_stream_callback.h"
#include "../../patterns.h"
#include "../../types.h"
#include "../i_physicalization_manager.h"
#include "../shared/c_recursive_srw_lock.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace SDK::wh::engine3d {
    /**
     * One brush a layer's `physics/layer<id>.dat` physicalizes, as the file
     * stores it and `C_PhysicalizationManager::BuildLayerColliders` reads it.
     *
     * Field meanings follow the reader: a material index, a collision class
     * index and the brush's render flags.
     */
    struct S_LayerPhysicsRecord {
        Matrix34 worldTransform;         // 0x00
        std::uint32_t staticObjectIndex; // 0x30, into the level's static object table
        std::uint32_t materialIndex;     // 0x34, read as a dword by BuildLayerColliders (0x180845740)
        std::uint16_t collisionClass;    // 0x38, read as a word
        std::byte m_unread003A[0x6];     // 0x3A, the reader skips it
        std::uint64_t renderFlags;       // 0x40
    };

    static_assert(offsetof(S_LayerPhysicsRecord, staticObjectIndex) == 0x30, "S_LayerPhysicsRecord::staticObjectIndex offset mismatch");
    static_assert(offsetof(S_LayerPhysicsRecord, collisionClass) == 0x38, "S_LayerPhysicsRecord::collisionClass offset mismatch");
    static_assert(offsetof(S_LayerPhysicsRecord, renderFlags) == 0x40, "S_LayerPhysicsRecord::renderFlags offset mismatch");
    static_assert(sizeof(S_LayerPhysicsRecord) == 0x48, "S_LayerPhysicsRecord size mismatch");

    /** The records of one layer file, as its loader hands them on. */
    struct S_LayerPhysicsRecordSpan {
        const S_LayerPhysicsRecord *records; // 0x00
        std::uint64_t count;                 // 0x08
    };

    static_assert(sizeof(S_LayerPhysicsRecordSpan) == 0x10, "S_LayerPhysicsRecordSpan size mismatch");

    /**
     * Owner of every precomputed static collider in the level.
     *
     * While `wh_e_PernamentPhysicalization` is on (the shipped default), a
     * brush never physicalizes itself -- `CBrush::PhysicalizeOnHeap`
     * (0x180530A3C) returns early for a brush flagged 0x4000000000000000 --
     * and this builds its collider instead: one anonymous `PE_STATIC` per
     * layer record, with no foreign data, kept in a per-layer list in record
     * order. So a brush's `IRenderNode::Dephysicalize` does not take its
     * collision away; suspending the entity here does.
     *
     * 0xA0 bytes, allocated and constructed (0x180A67D38) by the 3D engine,
     * which keeps it in a global. The list is an MSVC
     * `unordered_map<int, vector<IPhysicalEntity *>>` read in place: the mod's
     * own standard library does not share the game's layout in a debug build.
     */
    class C_PhysicalizationManager final: public I_PhysicalizationManager, public IStreamCallback, public Cry3DEngineBase {
      public:
        static C_PhysicalizationManager *GetInstance() {
            return *reinterpret_cast<C_PhysicalizationManager **>(gPatterns.C_PhysicalizationManager__Instance);
        }

        /** Guards the per-layer lists; recursive, held by the builder around every push. */
        shared::C_RecursiveSRWLock *GetLock() {
            return &m_lock;
        }

        /**
         * The colliders built for one layer, one per record in the order its
         * file lists them; empty when the layer has none. Only valid while
         * `GetLock()` is held.
         */
        std::span<IPhysicalEntity *const> GetLayerColliders(int layerId) const {
            for (const S_LayerCollidersNode *node = m_layerListHead->next; node != m_layerListHead; node = node->next) {
                if (node->layerId == layerId) {
                    return {node->begin, node->end};
                }
            }
            return {};
        }

      private:
        /** One `std::list` node of the map: links, then the pair. */
        struct S_LayerCollidersNode {
            S_LayerCollidersNode *next; // 0x00
            S_LayerCollidersNode *prev; // 0x08
            int layerId;                // 0x10
            std::byte m_padding0014[0x4];
            IPhysicalEntity **begin;    // 0x18
            IPhysicalEntity **end;      // 0x20
            IPhysicalEntity **capacity; // 0x28
        };

        static_assert(sizeof(S_LayerCollidersNode) == 0x30, "S_LayerCollidersNode size mismatch");

        shared::C_RecursiveSRWLock m_lock;      // 0x10
        float m_layerMaxLoadFactor;             // 0x20
        std::byte m_padding0024[0x4];           // the ctor (0x180A67D38) writes only the float at 0x20
        S_LayerCollidersNode *m_layerListHead;  // 0x28, the list's sentinel
        std::uint64_t m_layerCount;             // 0x30
        void **m_pBucketsBegin;                 // 0x38, the map's bucket vector
        void **m_pBucketsEnd;                   // 0x40
        void **m_pBucketsCapacity;              // 0x48
        std::uint64_t m_bucketMask;             // 0x50, ctor sets 7
        std::uint64_t m_maxBucketIndex;         // 0x58, ctor sets 8
        // Pending layer file reads: the same MSVC unordered_map shape, built by 0x1806E4638.
        float m_pendingMaxLoadFactor;           // 0x60
        std::byte m_padding0064[0x4];           // 0x64
        void *m_pPendingListHead;               // 0x68, the list's sentinel
        std::uint64_t m_pendingCount;           // 0x70
        void **m_pPendingBucketsBegin;          // 0x78
        void **m_pPendingBucketsEnd;            // 0x80
        void **m_pPendingBucketsCapacity;       // 0x88
        std::uint64_t m_pendingBucketMask;      // 0x90, set to 7
        std::uint64_t m_pendingMaxBucketIndex;  // 0x98, set to 8

        static void AssertLayout() {
            static_assert(offsetof(C_PhysicalizationManager, m_lock) == 0x10, "C_PhysicalizationManager::m_lock offset mismatch");
            static_assert(offsetof(C_PhysicalizationManager, m_layerListHead) == 0x28, "C_PhysicalizationManager::m_layerListHead offset mismatch");
            static_assert(offsetof(C_PhysicalizationManager, m_layerCount) == 0x30, "C_PhysicalizationManager::m_layerCount offset mismatch");
            static_assert(offsetof(C_PhysicalizationManager, m_bucketMask) == 0x50, "C_PhysicalizationManager::m_bucketMask offset mismatch");
            static_assert(offsetof(C_PhysicalizationManager, m_pendingMaxLoadFactor) == 0x60, "C_PhysicalizationManager::m_pendingMaxLoadFactor offset mismatch");
            static_assert(offsetof(C_PhysicalizationManager, m_pendingMaxBucketIndex) == 0x98, "C_PhysicalizationManager::m_pendingMaxBucketIndex offset mismatch");
        }
    };

    static_assert(sizeof(C_PhysicalizationManager) == 0xA0, "C_PhysicalizationManager size mismatch");
} // namespace SDK::wh::engine3d
