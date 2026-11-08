#pragma once

#include "shared/features/siege/stone_pile_entity.h"
#include "shared/features/siege/stone_pile_rpc.h"

#include <mafianet/virtual_world.h>
#include <mafianet/types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Siege {
    /**
     * The piles of hurling stones: how many stones each has left, who threw
     * one, and what a stone that came down hurt.
     *
     * The throw is the game's own on every client. The thrower's client
     * reports the grab (`StonePileUse`), which takes a stone off the count
     * and is relayed to everyone else near the pile to replay on the
     * thrower's body; then where its stone first met something
     * (`StoneImpactReport`). The server trusts that point only within a
     * stone's throw of the pile and only once per grab, hurts the NPCs around
     * it, and tells the players near it to take their share.
     */
    class StonePileService final {
      public:
        static StonePileService &Get();

        /** Registers the throwers' reports. Called once, at boot. */
        void Install();

        /** Forgets the player's grab. */
        void OnPlayerDisconnect(std::uint64_t playerNetworkId);

        Shared::Entities::StonePileEntity *Spawn(const glm::vec3 &position, const glm::quat &rotation, MafiaNet::VirtualWorldId virtualWorld);
        void Destroy(Shared::Entities::StonePileEntity *pile);
        std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        static std::vector<Shared::Entities::StonePileEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);
        static Shared::Entities::StonePileEntity *Find(std::uint64_t networkId);

      private:
        /** A grab the server accepted, kept until its window runs out: its stone's report and its leave are taken only against it. */
        struct PendingThrow {
            std::uint64_t pileNetworkId = 0;
            std::uint64_t grabbedAtMs   = 0;

            /** Where the thrower stood when they took the stone; the stone comes down within a throw of it. */
            glm::vec3 grabbedFrom {0.0F};
            bool reported = false;
            bool left     = false;
            bool thrown   = false;
        };

        void OnUse(const Shared::RPC::StonePileUse &use, MafiaNet::Packet *packet);
        void OnImpactReport(const Shared::RPC::StoneImpactReport &report, MafiaNet::Packet *packet);

        /** Drops the grabs whose window has run out. */
        void PruneExpired(std::uint64_t now);

        /** Sends `message` to every player in the pile's world near `point` but `exceptNetworkId`. */
        template <typename Message>
        void Relay(Message message, const Shared::Entities::StonePileEntity &pile, const glm::vec3 &point, float radius, std::uint64_t exceptNetworkId);

        /** By thrower: their last grab, until its window runs out. */
        std::unordered_map<std::uint64_t, PendingThrow> _pending;
    };
} // namespace KCDC::Features::Siege
