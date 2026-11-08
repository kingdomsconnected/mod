#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class IArea;
    class IAreaManagerEventListener;

    /** ABI view of CryEngine's pointer/count/capacity PodArray container. */
    template <typename T>
    struct CryPodArrayLayout {
        T *elements;
        std::int32_t count;
        std::int32_t capacity;
    };

    /** Exact result record produced by IAreaManager::QueryAudioAreas. */
    struct SAudioAreaInfo {
        IArea *area;                       // 0x00
        float amount;                      // 0x08
        std::uint32_t audioEnvironmentId;  // 0x0C
        EntityId environmentEntityId;      // 0x10
        std::uint32_t padding0014;
    };

    /**
     * Abstract spatial-area manager, vtable 0x184071C70.
     *
     * KCD2 emits exactly 17 entries. The standard spatial/audio query and
     * invalidation contracts are recovered below; four locally added or
     * signature-altered operations remain opaque.
     */
    class IAreaManager {
      public:
        virtual ~IAreaManager() = default; // +0x00
        virtual std::size_t GetAreaAmount() const = 0; // +0x08
        virtual const IArea *GetArea(std::size_t index) const = 0; // +0x10
        virtual void unk_fn0003() = 0;
        virtual std::size_t GetOverlappingAreas(
            const AABB &bounds, CryPodArrayLayout<IArea *> &areas) const = 0; // +0x20
        virtual bool QueryAudioAreas(
            const Vec3 &position, SAudioAreaInfo *results,
            std::size_t maximumResults, std::size_t &resultCount) = 0; // +0x28
        virtual bool GetLinkedAreas(
            EntityId linkedEntityId, EntityId *results,
            int &resultCapacityAndCount) const = 0; // +0x30
        virtual void DrawLinkedAreas(EntityId linkedEntityId) const = 0; // +0x38
        virtual void AddEventListener(IAreaManagerEventListener *listener) = 0; // +0x40
        virtual void RemoveEventListener(IAreaManagerEventListener *listener) = 0; // +0x48
        virtual void MarkEntityForUpdate(EntityId entityId) = 0; // +0x50
        virtual void unk_fn0011() = 0;
        virtual void SetAreasDirty() = 0; // +0x60
        virtual void SetAreaDirty(IArea *area) = 0; // +0x68
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void TriggerAudioListenerUpdate(const IArea *area) = 0; // +0x80
    };

    static_assert(sizeof(CryPodArrayLayout<IArea *>) == 0x10,
                  "CryPodArrayLayout size mismatch");
    static_assert(offsetof(SAudioAreaInfo, audioEnvironmentId) == 0x0C,
                  "SAudioAreaInfo::audioEnvironmentId offset mismatch");
    static_assert(offsetof(SAudioAreaInfo, environmentEntityId) == 0x10,
                  "SAudioAreaInfo::environmentEntityId offset mismatch");
    static_assert(sizeof(SAudioAreaInfo) == 0x18,
                  "SAudioAreaInfo size mismatch");
    static_assert(sizeof(IAreaManager) == 0x8, "IAreaManager size mismatch");
} // namespace SDK
