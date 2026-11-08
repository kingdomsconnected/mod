#pragma once

#include "features/siege/siege_common.h"

#include "shared/features/siege/siege_ladder_entity.h"
#include "shared/features/siege/siege_ladder_rpc.h"

#include <mafianet/virtual_world.h>
#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace KCDC::Features::Siege {
    /**
     * The siege ladders: where each stands, its push and its raise on the
     * server clock, and who may do either.
     *
     * A push plays the game's own halberd push on the defender
     * (`GuardHlbrdPushAway_Player`, tagged for the ladder's height) and the
     * ladder falls as the blade meets it, landing when the battle script's
     * crash would; a raise plays the attacker's `LayLadder`. Every client
     * tilts its ladder from the state and the clock and throws its own player
     * off one that starts to fall under them.
     */
    class SiegeLadderService final {
      public:
        static SiegeLadderService &Get();

        /** Registers the players' requests. Called once, at boot. */
        void Install();

        /** Ends the pushes and raises whose time has run out. */
        void Update();

        Shared::Entities::SiegeLadderEntity *Spawn(std::size_t assetIndex, const glm::vec3 &position, const glm::quat &rotation, MafiaNet::VirtualWorldId virtualWorld);
        void Destroy(Shared::Entities::SiegeLadderEntity *ladder);
        std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        static std::vector<Shared::Entities::SiegeLadderEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);
        static Shared::Entities::SiegeLadderEntity *Find(std::uint64_t networkId);

        /** Pushes a standing ladder off its wall; `pusherNetworkId`, when set, plays the push and is credited with it. */
        SiegeResult Push(Shared::Entities::SiegeLadderEntity &ladder, std::uint64_t pusherNetworkId);

        /** Raises a fallen ladder back against its wall; `raiserNetworkId`, when set, plays the lift. */
        SiegeResult Raise(Shared::Entities::SiegeLadderEntity &ladder, std::uint64_t raiserNetworkId);

        /** Breaks the ladder where it stands or lies: it is shown in pieces, lying, and nothing climbs or raises it. */
        void Break(Shared::Entities::SiegeLadderEntity &ladder);

        /** Puts a broken or fallen ladder back against its wall at once. */
        void Repair(Shared::Entities::SiegeLadderEntity &ladder);

      private:
        void OnUse(const Shared::RPC::SiegeLadderUse &use, MafiaNet::Packet *packet);
        void Settle(Shared::Entities::SiegeLadderEntity &ladder);

        /** The ladders this tick settles; kept so the tick does not allocate. */
        std::vector<std::uint64_t> _due;
    };
} // namespace KCDC::Features::Siege
