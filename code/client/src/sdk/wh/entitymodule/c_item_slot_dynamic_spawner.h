#pragma once

#include "../../cryengine/action/i_game_framework_listener.h"
#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_ItemSlot;

    /** Adaptive per-frame budget for spawning item entities. */
    struct DynamicSpawnBudgetLayout {
        std::int32_t m_currentBudget;
        std::int32_t m_minimumBudget;
        std::int32_t m_maximumBudget;
        std::int32_t m_increaseThreshold;
        std::int32_t m_decreaseThreshold;
        bool m_forceUnlimited;
        std::byte m_padding0015[0x3];
    };

    /** One 16-byte item-slot record partitioned between the search jobs. */
    struct DynamicSpawnCandidateLayout {
        C_ItemSlot *m_pItemSlot;
        std::uint64_t m_spatialQueryKey;
    };

    /** One decision emitted by an item-slot search job. */
    struct DynamicSpawnSearchResultLayout {
        C_ItemSlot *m_pItemSlot;
        float m_squaredDistance;
        bool m_shouldSpawn;
        std::byte m_padding000D[0x3];
    };

    template <typename T> struct DynamicSpawnVectorLayout {
        T *m_pBegin;
        T *m_pEnd;
        T *m_pCapacity;

        std::size_t GetCount() const {
            return m_pBegin != nullptr
                       ? static_cast<std::size_t>(m_pEnd - m_pBegin)
                       : 0;
        }
    };

    /** One of the two fixed asynchronous spawn-worker records. */
    struct DynamicSpawnWorkerLayout {
        void *m_pJobState;
        DynamicSpawnCandidateLayout *m_pCandidatesBegin; // 0x08
        DynamicSpawnCandidateLayout *m_pCandidatesEnd;   // 0x10
        DynamicSpawnVectorLayout<DynamicSpawnSearchResultLayout> m_results; // 0x18
        std::byte m_completionCallback[0x40]; // 0x30, native std::function
        std::uint32_t m_completionState; // 0x70, atomically reset
        std::byte m_padding0074[0x4];

        std::size_t GetCandidateCount() const {
            return m_pCandidatesBegin != nullptr
                       ? static_cast<std::size_t>(m_pCandidatesEnd -
                                                  m_pCandidatesBegin)
                       : 0;
        }
    };

    static_assert(sizeof(DynamicSpawnCandidateLayout) == 0x10,
                  "DynamicSpawnCandidateLayout size mismatch");
    static_assert(sizeof(DynamicSpawnBudgetLayout) == 0x18,
                  "DynamicSpawnBudgetLayout size mismatch");
    static_assert(sizeof(DynamicSpawnSearchResultLayout) == 0x10,
                  "DynamicSpawnSearchResultLayout size mismatch");
    static_assert(sizeof(DynamicSpawnVectorLayout<DynamicSpawnCandidateLayout>) ==
                      0x18,
                  "DynamicSpawnVectorLayout size mismatch");
    static_assert(sizeof(DynamicSpawnWorkerLayout) == 0x78,
                  "DynamicSpawnWorkerLayout size mismatch");

    /** Framework listener responsible for dynamic item-slot spawning. */
    class C_ItemSlotDynamicSpawner : public IGameFrameworkListener {
      public:
        ~C_ItemSlotDynamicSpawner() override = default;

        void OnPostUpdate(float) override {}
        void unk_fn0002() override {}
        void unk_fn0003() override {}
        void unk_fn0004() override {}
        void unk_fn0005(const SActionEvent &) override {}
        void unk_fn0006() override {}
        void unk_fn0007() override {}
        void unk_fn0008() override {}

        bool IsSlotTrackingActive() const { return m_slotTrackingActive != 0; }
        bool UsesParallelSearch() const { return m_parallelSearchEnabled != 0; }
        bool ProcessesFrameworkEvents() const {
            return m_parallelSearchEnabled != 0;
        }
        const DynamicSpawnBudgetLayout &GetSpawnBudget() const {
            return m_spawnBudget;
        }
        const Vec3 &GetSearchOrigin() const { return m_searchOrigin; }
        float GetInnerSpawnRadiusSquared() const {
            return m_innerSpawnRadiusSquared;
        }
        float GetOuterSpawnRadiusSquared() const {
            return m_outerSpawnRadiusSquared;
        }
        const DynamicSpawnVectorLayout<DynamicSpawnCandidateLayout> &
        GetSearchCandidates() const {
            return m_searchCandidates;
        }
        DynamicSpawnWorkerLayout *GetSpawnWorker(std::size_t index) {
            return index < 2 ? &m_spawnWorkers[index] : nullptr;
        }
        const DynamicSpawnWorkerLayout *GetSpawnWorker(std::size_t index) const {
            return index < 2 ? &m_spawnWorkers[index] : nullptr;
        }

      private:
        std::uint8_t m_slotTrackingActive; // 0x08, gates slot add/remove
        std::uint8_t m_parallelSearchEnabled; // 0x09, gates events 26/27
        std::byte m_padding000A[0x2];
        float m_innerSpawnRadiusSquared; // 0x0C
        float m_outerSpawnRadiusSquared; // 0x10
        DynamicSpawnBudgetLayout m_spawnBudget; // 0x14
        std::uint32_t m_state002C;
        Vec3 m_searchOrigin; // 0x30
        std::byte m_padding003C[0x4];
        void *m_pSpatialQuery; // 0x40
        DynamicSpawnVectorLayout<DynamicSpawnCandidateLayout>
            m_primarySlotEntries; // 0x48
        DynamicSpawnVectorLayout<DynamicSpawnCandidateLayout>
            m_secondarySlotEntries; // 0x60
        DynamicSpawnVectorLayout<DynamicSpawnCandidateLayout>
            m_searchCandidates; // 0x78
        std::uint64_t m_searchCursor; // 0x90
        DynamicSpawnWorkerLayout m_spawnWorkers[2]; // 0x98
    };

    static_assert(sizeof(C_ItemSlotDynamicSpawner) == 0x188,
                  "C_ItemSlotDynamicSpawner size mismatch");
} // namespace SDK::wh::entitymodule
