#pragma once

#include "alchemy_character_evaluator.h"
#include "alchemy_module.h"
#include "features/player/soul_profile.h"
#include "shared/features/crafting/smithing_rpc.h"
#include "smithing_module.h"

#include <mafianet/types.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::Crafting {
    /**
     * The network and scripting face of crafting. Alchemy requests arrive as
     * one JSON body per native intent, smithing ones as typed messages; both
     * are answered in the order they came and ruled on by the `AlchemyModule`
     * and `SmithingModule` this service owns. Everything is in memory for the
     * connection: nothing is saved, nothing survives a restart, and a
     * disconnect ends the player's session with its refunds.
     */
    class CraftingService final {
      public:
        CraftingService();

        void Install();
        void Update();
        void Reset();
        /** Sends the arriving client every recipe scripts registered. */
        void OnPlayerConnect(MafiaNet::PeerGuid peer);
        /** Sends every client the recipes scripts registered since it was last sent any. */
        void PublishRecipes();
        /** Ends the player's session, refunding into the inventory before `playerDisconnect` reads it. */
        void OnPlayerDisconnect(MafiaNet::PeerGuid peer);
        /** After `playerDisconnect`: drops the recipe knowledge a script could still read there. */
        void OnPlayerLeft(std::uint64_t playerId);

        /** The player's alchemy session or kept table, else their smithing workpiece, else null. */
        nlohmann::json Session(std::uint64_t playerId) const;
        nlohmann::json Occupancy(const std::string &station, std::uint32_t virtualWorld) const;
        /** Ends the player's alchemy session and smithing workpiece and closes them in their game. False when there was nothing to end. */
        bool Cancel(std::uint64_t playerId);
        /** The player holds an alchemy table, brewing or between batches. */
        bool HoldsAlchemy(std::uint64_t playerId) const;
        nlohmann::json Knowledge(std::uint64_t playerId) const;
        /** Sets one recipe's known-steps mask and pushes the change to the owner. */
        bool SetKnowledge(std::uint64_t playerId, const std::string &recipe, std::uint32_t mask);

      private:
        static constexpr std::size_t kMaximumQueuedRequests = 256;
        static constexpr std::size_t kMaximumQueuedPerPeer  = 8;

        struct Incoming {
            MafiaNet::PeerGuid peer;
            std::string body;
        };
        struct IncomingSmithing {
            MafiaNet::PeerGuid peer;
            std::optional<KCDC::Shared::RPC::SmithingBegin> begin;
            std::optional<KCDC::Shared::RPC::SmithingFinish> finish;
        };

        static std::uint64_t NowMs();
        static AlchemyHooks Hooks();
        static SmithingHooks SmithingHooksFor();
        void ReceiveSmithing(IncomingSmithing message);
        void HandleSmithing(const IncomingSmithing &message);
        std::optional<SmithingActor> SmithingActorOf(std::uint64_t playerId) const;
        void PushSmithingClosed(std::uint64_t playerId, std::uint32_t session, KCDC::Shared::RPC::SmithingCloseReason reason) const;
        /** The soul the player's accepted progression, reported permanent buffs and health describe. */
        bool Snapshot(std::uint64_t playerId, KCDC::Features::Player::SoulSnapshot &soul, std::string &error) const;
        void Receive(MafiaNet::PeerGuid peer, const std::string &body);
        void Handle(MafiaNet::PeerGuid peer, const nlohmann::json &request);
        std::optional<AlchemyActor> Actor(std::uint64_t playerId) const;
        /** The player's character, from their accepted progression, their reported permanent buffs and their health. */
        bool Evaluate(std::uint64_t playerId, AlchemyCharacterContext &context, std::string &error) const;
        nlohmann::json CharacterView(std::uint64_t playerId, const AlchemyCharacterContext &context) const;
        nlohmann::json Response(std::uint64_t playerId, const std::string &request, const std::string &op, const AlchemyReply &reply) const;
        /** A message nobody asked for: `character` after a knowledge change, `closed` when the server ended the session. */
        void Push(std::uint64_t playerId, const std::string &op, const std::string &reason = {});
        void Send(MafiaNet::PeerGuid peer, const nlohmann::json &response) const;
        /** Raises inventory changes first, so a crafting handler reads the rows it describes. */
        void DispatchEvents();

        /** Sends `peer` the registered recipes from the `from`th on. */
        void SendRecipes(MafiaNet::PeerGuid peer, std::size_t from) const;

        AlchemyModule _alchemy;
        SmithingModule _smithing;
        // How many of the scripts' recipes each connected client has been sent.
        std::map<MafiaNet::PeerGuid, std::size_t> _recipesSent;
        std::mutex _incomingMutex;
        std::vector<Incoming> _incoming;
        std::vector<IncomingSmithing> _incomingSmithing;
    };
} // namespace KCDC::Features::Crafting
