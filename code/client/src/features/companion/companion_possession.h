#pragma once

#include "sdk/wh/rpgmodule/c_soul.h"

#include <cstdint>

namespace KCDC::Features::Companion {
    /**
     * What stating an animal's master changed.
     *
     * `Refused` is `PossessAnimal` saying no to an animal it had just been
     * handed free of any master, which leaves it nobody's.
     */
    enum class PossessionChange : std::uint8_t {
        None,
        Possessed,
        Released,
        Refused,
    };

    /**
     * The soul of the player with this NetworkID on this client, or nullptr
     * while they have no body here.
     *
     * Two answers for one question because the local player is not a copy.
     * A remote player's soul lives on their copy's actor. The local `Human`
     * replica has no entity at all -- `Human::Frame` despawns any body it
     * finds on an owned replica, because the local player already *is* the
     * game's player -- so asking it for an actor answers nullptr forever, and
     * an animal resolving its master that way is never possessed by anybody.
     */
    SDK::wh::rpgmodule::C_Soul *ResolvePlayerSoul(std::uint64_t playerNetworkId);

    /**
     * Makes `animal` a companion of `master`'s soul group, or nobody's when
     * `master` is null. The game's own notion of whose horse or dog this is,
     * held on the souls of this client alone, so every peer states it for
     * itself from the replicated owner.
     *
     * `PossessAnimal` refuses an animal that already has a master, so a
     * hand-over is release-then-possess. The animal goes to the head of its
     * category, which is what makes it the *primary* companion -- the one
     * the game's horse and dog paths answer with.
     */
    PossessionChange ReconcileMaster(SDK::wh::rpgmodule::C_Soul *animal, SDK::wh::rpgmodule::C_Soul *master);

    /**
     * Takes `animal` away from whoever it belongs to. Called before its body
     * goes, or its master keeps a companion pointer into an animal that no
     * longer exists.
     */
    void ReleaseFromMaster(SDK::wh::rpgmodule::C_Soul *animal);
} // namespace KCDC::Features::Companion
