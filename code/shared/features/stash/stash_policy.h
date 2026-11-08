#pragma once

#include "shared/features/stash/stash_rpc.h"

namespace KCDC::Shared::Game {
    /**
     * What AnimStash.lua offers is a reach of `fUseDistance` (2.5 m) to open
     * and `fUseLockpickDistance` (2 m) to pick, from the entity's pivot. This
     * covers the chest's own size and a replicated position that trails the
     * body by a few ticks.
     */
    constexpr float kStashReachMargin = 3.0F;

    /** The reach a container that names none is given: AnimStash.lua's default. */
    constexpr float kDefaultStashUseDistance = 2.5F;

    /** What the server holds about a container when a player asks to use it. */
    struct StashRequest {
        KCDC::Shared::RPC::StashAction action = KCDC::Shared::RPC::StashAction::Open;

        bool locked       = false;
        bool lockpickable = true;

        /** Somebody else has its contents open. */
        bool heldByOther = false;

        float useDistance = kDefaultStashUseDistance;

        /** From the body to the container's pivot, squared. */
        float distanceSquared = 0.0F;
    };

    /**
     * Why the game's own rules forbid a request, or null when they allow it.
     * A script may still refuse what this allows.
     *
     * An unlock is a key turned in AnimStash:OnUsed; which key is the client's
     * business -- only it can see its own inventory -- so the server checks
     * only that there is a lock to turn. A lockpick is the minigame starting.
     */
    inline const char *EvaluateStashRequest(const StashRequest &request) {
        const float reach = request.useDistance + kStashReachMargin;
        if (!(request.distanceSquared <= reach * reach)) {
            return "the body is out of reach of the container";
        }
        if (request.heldByOther) {
            return "another player has the container open";
        }

        switch (request.action) {
        case KCDC::Shared::RPC::StashAction::Open:
            if (request.locked) {
                return "the container is locked";
            }
            break;
        case KCDC::Shared::RPC::StashAction::Unlock:
            if (!request.locked) {
                return "the container is not locked";
            }
            break;
        case KCDC::Shared::RPC::StashAction::Lockpick:
            if (!request.locked) {
                return "the container is not locked";
            }
            if (!request.lockpickable) {
                return "the container cannot be lockpicked";
            }
            break;
        }
        return nullptr;
    }
} // namespace KCDC::Shared::Game
