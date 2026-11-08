#pragma once

#include "i_entity_pool_manager.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CEntitySystem;

    /** One of the six listener groups used while pool events are dispatched. */
    struct EntityPoolListenerBucketLayout {
        void **listeners;      // 0x00
        void **listenersEnd;   // 0x08
        void **listenersCapacity; // 0x10
        std::size_t dispatchDepth; // 0x18
        std::uint16_t cleanupPending; // 0x20
        std::byte padding0022[0x6];
    };

    static_assert(sizeof(EntityPoolListenerBucketLayout) == 0x28,
                  "EntityPoolListenerBucketLayout size mismatch");

    /**
     * Concrete pooled-spawn manager owned by CEntitySystem.
     *
     * Its table at 0x184076848 contains exactly the 14
     * IEntityPoolManager entries and adds no primary virtual operations. The
     * owner allocation and scalar deleting destructor prove size 0xA8.
     */
    class CEntityPoolManager : public IEntityPoolManager {
      public:
        ~CEntityPoolManager() override = default;

        bool IsEnabled() const { return m_enabled; }
        CEntitySystem *GetEntitySystem() const { return m_pEntitySystem; }

      private:
        bool m_enabled; // 0x08
        std::byte m_padding0009[0x7];
        void *m_pState0010;             // 0x10
        CEntitySystem *m_pEntitySystem; // 0x18
        void *m_pState0020;             // 0x20
        EntityPoolListenerBucketLayout *m_pListenerBuckets; // 0x28
        EntityPoolListenerBucketLayout *m_pListenerBucketsEnd; // 0x30
        EntityPoolListenerBucketLayout *m_pListenerBucketsCapacity; // 0x38
        void *m_pPoolTreeHead;          // 0x40
        std::size_t m_poolCount;        // 0x48
        void *m_pClassTreeHead;         // 0x50
        std::size_t m_classCount;       // 0x58
        void *m_pEntityTreeHead;        // 0x60
        std::size_t m_entityCount;      // 0x68
        void **m_pPendingEntries;       // 0x70
        void **m_pPendingEntriesEnd;    // 0x78
        void **m_pPendingEntriesCapacity; // 0x80
        std::byte m_opaque0088[0x14];
        SPreparingParams m_preparingParams; // 0x9C
        bool m_isPreparingEntity;           // 0xA4
        std::byte m_padding00A5[0x3];
    };

    static_assert(sizeof(CEntityPoolManager) == 0xA8, "CEntityPoolManager size mismatch");
} // namespace SDK
