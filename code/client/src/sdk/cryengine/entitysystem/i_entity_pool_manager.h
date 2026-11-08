#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK {
    class IEntityPoolListener;

    /**
     * IEntityPoolManager, vtable 0x1840767D0. The KCD2 interface contains
     * exactly 14 entries. Each operation is independently matched to the
     * concrete implementation's arguments and behavior, preserving KCD2's
     * exact slot order.
     */
    class IEntityPoolManager {
      public:
        struct SPreparingParams {
            EntityId entityId;
            std::uint32_t aiObjectId;
        };

        virtual ~IEntityPoolManager() = default; // +0x00
        virtual void Serialize(TSerialize serialize) = 0;
        virtual void Enable(bool enable) = 0;
        virtual void ResetPools(bool saveState = true) = 0;
        virtual void AddListener(IEntityPoolListener *listener,
                                 const char *who,
                                 std::uint32_t subscriptions) = 0;
        virtual void RemoveListener(IEntityPoolListener *listener) = 0;
        virtual bool PrepareFromPool(EntityId entityId,
                                     bool prepareNow = false) = 0;
        virtual bool ReturnToPool(EntityId entityId,
                                  bool saveState = true) = 0;
        virtual void ResetBookmark(EntityId entityId) = 0;
        virtual bool IsClassDefaultBookmarked(const char *className) const = 0;
        virtual bool IsEntityBookmarked(EntityId entityId) const = 0;
        virtual const char *GetBookmarkedClassName(EntityId entityId) const = 0;
        virtual const char *GetBookmarkedEntityName(EntityId entityId) const = 0;
        virtual bool IsPreparingEntity(SPreparingParams &outParams) const = 0;
    };

    static_assert(sizeof(IEntityPoolManager::SPreparingParams) == 0x8,
                  "IEntityPoolManager::SPreparingParams size mismatch");
    static_assert(sizeof(IEntityPoolManager) == 0x8, "IEntityPoolManager size mismatch");
} // namespace SDK
