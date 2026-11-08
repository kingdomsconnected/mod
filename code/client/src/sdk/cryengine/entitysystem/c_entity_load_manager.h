#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CEntitySystem;

    /** Three-pointer MSVC vector storage used by CEntityLoadManager. */
    struct EntityLoadVectorLayout {
        void *begin;    // 0x00
        void *end;      // 0x08
        void *capacity; // 0x10
    };

    /** Two-word MSVC ordered-tree storage used by CEntityLoadManager. */
    struct EntityLoadTreeLayout {
        void *head;       // 0x00
        std::size_t size; // 0x08
    };

    static_assert(sizeof(EntityLoadVectorLayout) == 0x18,
                  "EntityLoadVectorLayout size mismatch");
    static_assert(sizeof(EntityLoadTreeLayout) == 0x10,
                  "EntityLoadTreeLayout size mismatch");

    /**
     * Native entity XML parser and batch-creation coordinator.
     *
     * CEntitySystem constructs this non-polymorphic object at its +0x600268
     * owner field. The allocation and scalar deletion both prove the exact
     * 0x90-byte extent. Its PrepareBatchCreation/OnBatchCreationCompleted
     * pair backs IEntitySystem::BeginCreateEntities/EndCreateEntities, while
     * LoadEntities parses each Entity node and drives the normal entity
     * spawn, initialization, attachment, transform, and listener lifecycle.
     *
     * The four vectors have constructor/destructor-proven element strides of
     * 0x48, 0x10, 0x10, and 0x20 respectively. Their stripped record type
     * names are deliberately not guessed here.
     */
    class CEntityLoadManager {
      public:
        CEntitySystem *GetEntitySystem() const { return m_pEntitySystem; }
        std::uint32_t GetBatchCreationDepth() const {
            return m_batchCreationDepth;
        }
        bool IsSlicedLoadActive() const { return m_slicedLoadActive; }

        const EntityLoadVectorLayout &GetParsedEntityRecords() const {
            return m_parsedEntityRecords;
        }
        const EntityLoadVectorLayout &GetPendingAttachmentRecords() const {
            return m_pendingAttachmentRecords;
        }
        const EntityLoadVectorLayout &GetPendingTargetRecords() const {
            return m_pendingTargetRecords;
        }
        const EntityLoadVectorLayout &GetDeferredRecords() const {
            return m_deferredRecords;
        }
        const EntityLoadTreeLayout &GetIdentifierMap() const {
            return m_identifierMap;
        }

        std::size_t GetParsedEntityRecordCount() const {
            return GetRecordCount(m_parsedEntityRecords, 0x48);
        }
        std::size_t GetPendingAttachmentRecordCount() const {
            return GetRecordCount(m_pendingAttachmentRecords, 0x10);
        }
        std::size_t GetPendingTargetRecordCount() const {
            return GetRecordCount(m_pendingTargetRecords, 0x10);
        }
        std::size_t GetDeferredRecordCount() const {
            return GetRecordCount(m_deferredRecords, 0x20);
        }

      private:
        static std::size_t GetRecordCount(const EntityLoadVectorLayout &vector,
                                          std::size_t stride) {
            if (vector.begin == nullptr || vector.end == nullptr) {
                return 0;
            }

            const auto begin = reinterpret_cast<const std::byte *>(vector.begin);
            const auto end = reinterpret_cast<const std::byte *>(vector.end);
            return static_cast<std::size_t>(end - begin) / stride;
        }

        EntityLoadVectorLayout m_parsedEntityRecords; // 0x00, stride 0x48
        std::uint32_t m_batchCreationDepth;            // 0x18
        bool m_slicedLoadActive;                       // 0x1C
        std::byte m_padding001D[0x3];
        EntityLoadVectorLayout m_pendingAttachmentRecords; // 0x20, stride 0x10
        EntityLoadVectorLayout m_pendingTargetRecords; // 0x38, stride 0x10
        EntityLoadTreeLayout m_state0050;              // 0x50
        CEntitySystem *m_pEntitySystem;                 // 0x60
        EntityLoadVectorLayout m_deferredRecords;       // 0x68, stride 0x20
        EntityLoadTreeLayout m_identifierMap;           // 0x80
    };

    static_assert(sizeof(CEntityLoadManager) == 0x90,
                  "CEntityLoadManager size mismatch");
} // namespace SDK
