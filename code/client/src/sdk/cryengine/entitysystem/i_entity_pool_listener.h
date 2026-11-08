#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class IEntity;
    struct SEntitySpawnParams;

    /**
     * IEntityPoolListener, vtable 0x183A410D0. The KCD2 interface contains
     * exactly seven entries. CEntityPoolManager's subscription masks and
     * concrete dispatch sites preserve the six callback contracts in this
     * exact order.
     */
    class IEntityPoolListener {
      public:
        enum Subscription : std::uint32_t {
            PoolBookmarkCreated = 1U << 0,
            EntityPreparedFromPool = 1U << 1,
            EntityReturningToPool = 1U << 2,
            EntityReturnedToPool = 1U << 3,
            PoolDefinitionsLoaded = 1U << 4,
            BookmarkEntitySerialize = 1U << 5,
        };

        static constexpr std::size_t SubscriptionCount = 6;

        virtual ~IEntityPoolListener() = default; // +0x00
        virtual void OnPoolBookmarkCreated(EntityId entityId,
                                           const SEntitySpawnParams &params,
                                           XmlNodeRef entityNode) = 0;
        virtual void OnEntityPreparedFromPool(EntityId entityId,
                                              IEntity *entity) = 0;
        virtual void OnEntityReturningToPool(EntityId entityId,
                                             IEntity *entity) = 0;
        virtual void OnEntityReturnedToPool(EntityId entityId,
                                            IEntity *entity) = 0;
        virtual void OnPoolDefinitionsLoaded(std::size_t numAI) = 0;
        virtual void OnBookmarkEntitySerialize(TSerialize serialize,
                                               void *entity) = 0; // +0x30
    };

    static_assert(sizeof(IEntityPoolListener) == 0x8,
                  "IEntityPoolListener size mismatch");
} // namespace SDK
