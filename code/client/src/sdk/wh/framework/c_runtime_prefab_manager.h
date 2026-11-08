#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::framework {
    struct RuntimePrefabTreeLayout {
        void *m_pHead;
        std::size_t m_size;
    };

    struct RuntimePrefabVectorLayout {
        void **m_pBegin;
        void **m_pEnd;
        void **m_pCapacity;

        std::size_t GetCount() const {
            return m_pBegin != nullptr
                       ? static_cast<std::size_t>(m_pEnd - m_pBegin)
                       : 0;
        }
    };

    struct RuntimePrefabHashMapLayout {
        float m_maxLoadFactor;
        std::uint32_t m_padding0004;
        void *m_pListHead;
        std::size_t m_size;
        void **m_pBucketsBegin;
        void **m_pBucketsEnd;
        void **m_pBucketsCapacity;
        std::size_t m_bucketMask;
        std::size_t m_maxBucket;
    };

    /**
     * Descriptive overlay for the manager's stripped 0x88-byte per-root
     * runtime-prefab record.
     */
    struct RuntimePrefabInstanceLayout {
        void *m_pPrefabTemplate; // 0x00
        Matrix34 m_localTransform; // 0x08
        EntityId m_rootEntityId; // 0x38
        std::uint32_t m_padding003C;
        std::byte m_auxiliaryVector0040[0x18];
        std::byte m_auxiliaryVector0058[0x18];
        RuntimePrefabVectorLayout m_spawnedChildren; // 0x70

        std::size_t GetSpawnedChildCount() const {
            return m_spawnedChildren.GetCount();
        }
    };

    static_assert(sizeof(RuntimePrefabTreeLayout) == 0x10,
                  "RuntimePrefabTreeLayout size mismatch");
    static_assert(sizeof(RuntimePrefabVectorLayout) == 0x18,
                  "RuntimePrefabVectorLayout size mismatch");
    static_assert(sizeof(RuntimePrefabHashMapLayout) == 0x40,
                  "RuntimePrefabHashMapLayout size mismatch");
    static_assert(offsetof(RuntimePrefabInstanceLayout, m_rootEntityId) == 0x38,
                  "RuntimePrefabInstanceLayout root id offset mismatch");
    static_assert(offsetof(RuntimePrefabInstanceLayout, m_spawnedChildren) == 0x70,
                  "RuntimePrefabInstanceLayout child vector offset mismatch");
    static_assert(sizeof(RuntimePrefabInstanceLayout) == 0x88,
                  "RuntimePrefabInstanceLayout size mismatch");

    /**
     * Owns runtime-prefab templates and the instances attached to root
     * entities. The native constructor proves the complete 0x68-byte extent.
     */
    class C_RuntimePrefabManager {
      public:
        virtual ~C_RuntimePrefabManager() = default;

        static C_RuntimePrefabManager *GetInstance();

        std::size_t GetTrackedPrefabCount() const {
            return m_prefabsByRootEntity.m_size;
        }

        /**
         * Registers a prefab template on an existing root entity and,
         * optionally, instantiates its contents immediately.
         */
        void InvokeCreatePrefabRaw(const CryGUID &prefabTemplateId,
                                   EntityId entityId, int maxSpawn,
                                   bool spawnImmediately);

        void CreatePrefab(const CryGUID &prefabTemplateId, EntityId entityId,
                          int maxSpawn = 0) {
            InvokeCreatePrefabRaw(prefabTemplateId, entityId, maxSpawn, false);
        }

        void SpawnPrefab(const CryGUID &prefabTemplateId, EntityId entityId,
                         int maxSpawn = 0) {
            InvokeCreatePrefabRaw(prefabTemplateId, entityId, maxSpawn, true);
        }

        /** Recomputes the spawned prefab contents from the root transform. */
        void InvokeMovePrefabRaw(EntityId entityId);

        /** Removes a runtime-prefab instance and its manager record. */
        void InvokeDeletePrefabRaw(EntityId entityId);

      private:
        RuntimePrefabTreeLayout m_prefabsByRootEntity; // 0x08
        RuntimePrefabTreeLayout m_templateSpawnCounts; // 0x18
        RuntimePrefabHashMapLayout m_state0028; // 0x28
    };

    static_assert(sizeof(C_RuntimePrefabManager) == 0x68,
                  "C_RuntimePrefabManager size mismatch");
} // namespace SDK::wh::framework
