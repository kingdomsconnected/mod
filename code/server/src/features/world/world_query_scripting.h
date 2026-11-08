#pragma once

#include "shared/features/world/world_query_rpc.h"

#include <v8.h>
#include <v8pp/module.hpp>

#include <cstdint>

namespace KCDC::Scripting {
    /**
     * Puts the three world questions on the `World` global, as promises.
     * Appended to the module `world_scripting.cpp` builds rather than
     * published on its own, because a global can only be published once.
     * Every verb takes a player; `WorldQueryService` carries why.
     */
    void RegisterWorldQueryBindings(v8::Isolate *isolate, v8pp::module &world);

    namespace Events {
        /** A player's swing met something solid that is not a body, as their client reported it. */
        void PlayerWeaponHit(std::uint64_t playerNetworkId, const KCDC::Shared::RPC::WorldRayHit &hit);
    } // namespace Events
} // namespace KCDC::Scripting
