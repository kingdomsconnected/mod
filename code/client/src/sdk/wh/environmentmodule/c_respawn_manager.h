#pragma once

#include "../../cryengine/3dengine/i_merge_mesh_stream_listener.h"
#include "../../patterns.h"
#include "../../types.h"
#include "i_respawn_manager.h"
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::environmentmodule {
    /** ABI header shared by C_RespawnManager's four native hash maps. */
    struct RespawnStateMapLayout {
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

    struct RespawnPointerVectorLayout {
        void **m_pBegin;
        void **m_pEnd;
        void **m_pCapacity;
    };

    /** Native claim payload accepted by RestoreHarvestedArea (0x182ADEAAC). */
    struct RespawnClaimLayout {
        SDK::Vec3 position;
        float radius;
        std::uint8_t kind;
        std::byte m_padding0011[7];
        std::uint64_t until;
    };
    static_assert(sizeof(RespawnClaimLayout) == 0x20);
    static_assert(offsetof(RespawnClaimLayout, position) == 0);
    static_assert(offsetof(RespawnClaimLayout, radius) == 0xC);
    static_assert(offsetof(RespawnClaimLayout, kind) == 0x10);
    static_assert(offsetof(RespawnClaimLayout, until) == 0x18);

    /** Native unordered-set node: allocation 0x180451E4C, copy 0x182ADBD88. */
    struct RespawnClaimNodeLayout {
        RespawnClaimNodeLayout *next;
        RespawnClaimNodeLayout *previous;
        RespawnClaimLayout claim;
    };
    static_assert(sizeof(RespawnClaimNodeLayout) == 0x30);
    static_assert(offsetof(RespawnClaimNodeLayout, claim) == 0x10);

    /** Current merged-mesh respawn candidate tracked near the player. */
    struct RespawnInteractionStateLayout {
        void *m_renderNode;
        Vec3 m_position;               // 0x08
        std::uint8_t m_pickableAreaId; // 0x14
        bool m_available;              // 0x15
        std::byte m_padding0016[2]; // its ctor 0x180E3C63C writes only the word at 0x14; no C_RespawnManager method touches 0x16
        SDK::AABB m_bounds; // 0x18
    };

    static_assert(sizeof(RespawnStateMapLayout) == 0x40, "RespawnStateMapLayout size mismatch");
    static_assert(sizeof(RespawnPointerVectorLayout) == 0x18, "RespawnPointerVectorLayout size mismatch");
    static_assert(sizeof(RespawnInteractionStateLayout) == 0x30, "RespawnInteractionStateLayout size mismatch");

    static_assert(offsetof(RespawnInteractionStateLayout, m_position) == 0x08);
    static_assert(offsetof(RespawnInteractionStateLayout, m_pickableAreaId) == 0x14);
    static_assert(offsetof(RespawnInteractionStateLayout, m_available) == 0x15);
    static_assert(offsetof(RespawnInteractionStateLayout, m_bounds) == 0x18);

    /**
     * Environment manager for streamed vegetation and world-item respawning.
     *
     * RTTI proves I_RespawnManager at +0x00 and
     * IMergeMeshStreamListener at +0x08. The environment module allocates
     * exactly 0x1C8 bytes. Construction establishes the player/query state,
     * eight respawn CVars, four native hash maps and one pointer vector;
     * destruction independently releases all five containers.
     */
    class C_RespawnManager
        : public I_RespawnManager
        , public SDK::IMergeMeshStreamListener {
      public:
        ~C_RespawnManager() override = default;

        const Vec3 &GetLastQueryPosition() const {
            return m_lastQueryPosition;
        }
        const RespawnInteractionStateLayout &GetInteractionState() const {
            return m_interactionState;
        }
        bool HasActiveInteraction() const {
            return m_interactionState.m_pickableAreaId != 0 && m_interactionState.m_available;
        }
        std::uint32_t HarvestArea(const RespawnInteractionStateLayout &area, std::uint64_t respawnTime, float radius) {
            return hook::this_call<std::uint32_t>(SDK::gPatterns.C_RespawnManager__HarvestArea, this, &area, respawnTime, radius);
        }
        /**
         * Every source record of `kind` whose availability is `available`
         * within planar `radius` of `position`: what HarvestArea harvests and
         * counts. The vector is the native's static scratch (0x1855E11C0),
         * valid until the next query on the main thread.
         */
        const RespawnPointerVectorLayout *QuerySources(const Vec3 &position, float radius, std::uint8_t kind, bool available) {
            return hook::this_call<const RespawnPointerVectorLayout *>(SDK::gPatterns.C_RespawnManager__QuerySources, this, &position, radius, kind, available);
        }
        /** Mirrors HarvestSelected's accepted-source proxy invalidation. */
        void ClearInteractionAt(const RespawnInteractionStateLayout &area, float radius) {
            const float dx = m_interactionState.m_position.x - area.m_position.x, dy = m_interactionState.m_position.y - area.m_position.y;
            if (m_interactionState.m_pickableAreaId == area.m_pickableAreaId && dx * dx + dy * dy <= radius * radius) {
                m_interactionState.m_pickableAreaId = 0;
                m_previousTrackedEntityId           = 0;
            }
        }
        /** Expiry is excluded from native equality/hash (0x182ADC764). */
        bool SetHarvestDeadline(const RespawnInteractionStateLayout &area, float radius, std::uint64_t until) {
            if (auto *node = FindHarvestClaim(area, radius)) {
                node->claim.until = until;
                return true;
            }
            return false;
        }
        /** Restore indexed vegetation now, then remove the claim for later streaming. */
        void RestoreHarvestedArea(const RespawnInteractionStateLayout &area, float radius) {
            RespawnClaimLayout claim {};
            claim.position = area.m_position;
            claim.radius   = radius;
            claim.kind     = area.m_pickableAreaId;
            hook::this_call<bool>(SDK::gPatterns.C_RespawnManager__RestoreHarvestedArea, this, &claim);
            if (auto *node = FindHarvestClaim(area, radius)) {
                hook::this_call<RespawnClaimNodeLayout *>(SDK::gPatterns.C_RespawnManager__EraseHarvestClaim, &m_primaryStateMap, node);
            }
        }
        RespawnClaimNodeLayout *FindHarvestClaim(const RespawnInteractionStateLayout &area, float radius) const {
            auto *head = static_cast<RespawnClaimNodeLayout *>(m_primaryStateMap.m_pListHead);
            if (head == nullptr) {
                return nullptr;
            }
            for (auto *node = head->next; node != head; node = node->next) {
                if (node->claim.position.x == area.m_position.x && node->claim.position.y == area.m_position.y && node->claim.position.z == area.m_position.z && node->claim.radius == radius && node->claim.kind == area.m_pickableAreaId) {
                    return node;
                }
            }
            return nullptr;
        }
        EntityId GetTrackedEntityId() const {
            return m_trackedEntityId;
        }
        const RespawnPointerVectorLayout &GetPendingObjects() const {
            return m_pendingObjects;
        }
        const RespawnStateMapLayout *GetStateMap(std::size_t index) const {
            switch (index) {
            case 0: return &m_primaryStateMap;
            case 1: return &m_streamedStateMaps[0];
            case 2: return &m_streamedStateMaps[1];
            case 3: return &m_streamedStateMaps[2];
            default: return nullptr;
            }
        }

      private:
        Vec3 m_lastQueryPosition; // 0x10
        std::byte m_padding001C[0x4];
        void *m_pRespawnCVars[8];                         // 0x20
        RespawnInteractionStateLayout m_interactionState; // 0x60
        EntityId m_trackedEntityId;                       // 0x90
        EntityId m_previousTrackedEntityId;               // 0x94
        void *m_pState0098;
        RespawnStateMapLayout m_primaryStateMap;      // 0xA0
        RespawnPointerVectorLayout m_pendingObjects;  // 0xE0
        RespawnStateMapLayout m_streamedStateMaps[3]; // 0xF8
        bool m_state01B8;
        std::byte m_padding01B9[0x3];
        std::uint32_t m_state01BC;
        bool m_state01C0;
        std::byte m_padding01C1[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_RespawnManager, m_interactionState) == 0x60);
            static_assert(offsetof(C_RespawnManager, m_trackedEntityId) == 0x90);
            static_assert(offsetof(C_RespawnManager, m_previousTrackedEntityId) == 0x94);
            static_assert(offsetof(C_RespawnManager, m_primaryStateMap) == 0xA0);
        }
    };

    static_assert(sizeof(C_RespawnManager) == 0x1C8, "C_RespawnManager size mismatch");
} // namespace SDK::wh::environmentmodule
