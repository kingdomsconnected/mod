#pragma once

#include <cstddef>

namespace SDK {
    class IEntity;
    struct SEntityEvent;

    struct IEntityEventListener {
        virtual ~IEntityEventListener() = default;

        /**
         * One event on one entity, for a listener registered through
         * `IEntitySystem::AddEntityEventListener`. +0x08, from the dispatch in
         * `CEntitySystem::OnEntityEvent` (0x1804B658C).
         *
         * An area event arrives on the entity that *owns* the area, not the
         * one that crossed it; who crossed is `event->Param(0)`.
         */
        virtual void OnEntityEvent(IEntity *entity, SEntityEvent *event) = 0; // +0x08
    };

    struct EntityEventListenerWithCleanup : IEntityEventListener {
        ~EntityEventListenerWithCleanup() override = default;
    };

    static_assert(sizeof(IEntityEventListener) == 0x8,
                  "IEntityEventListener size mismatch");
    static_assert(sizeof(EntityEventListenerWithCleanup) == 0x8,
                  "EntityEventListenerWithCleanup size mismatch");
} // namespace SDK
