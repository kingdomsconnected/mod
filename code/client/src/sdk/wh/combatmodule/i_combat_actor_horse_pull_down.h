#pragma once

#include "../../types.h"

namespace SDK::wh::combatmodule {
    /**
     * Pulling a rider off the horse they are on.
     *
     * Reached from an actor's combat component, and asked about the *victim's*
     * entity rather than the horse: the game's own `CanHorsePullDown` and
     * `RequestHorsePullDown` script bindings are these two slots, both taking
     * an entity id.
     */
    struct I_CombatActorHorsePullDown {
        virtual ~I_CombatActorHorsePullDown() = default;                  // slot 0

        /** Whether this actor may pull the given one down right now. */
        virtual bool CanPullDown(SDK::EntityId victimEntityId) = 0;       // slot 1, +0x08

        /** Starts the pull-down. The engine plays it out and decides the outcome. */
        virtual void RequestPullDown(SDK::EntityId victimEntityId) = 0;   // slot 2, +0x10
    };
} // namespace SDK::wh::combatmodule
