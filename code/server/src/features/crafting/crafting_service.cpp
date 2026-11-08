#include "crafting_service.h"
#include "crafting_scripting.h"
#include "features/smart_object/smart_object_service.h"

#include "core/server.h"
#include "features/player/player_roster.h"
#include "shared/features/crafting/alchemy_protocol.h"
#include "shared/features/crafting/custom_recipe_message.h"
#include "shared/features/crafting/smithing_native_ids.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/buff_catalog.h"
#include "shared/game/buff_rule_catalog.h"
#include "shared/game/crafting_catalog.h"
#include "shared/game/custom_recipe_registry.h"
#include "shared/game/dlc_catalog.h"
#include "shared/game/progression_catalog.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <set>

namespace KCDC::Features::Crafting {
    namespace {
        constexpr const char *kLogger = "Crafting";

        std::uint16_t RequiredDLC() {
            std::uint16_t result = 0;
            for (const auto &name : Server::Get()->GetModConfig().Get<std::vector<std::string>>("required_dlc")) {
                if (const auto *dlc = KCDC::Shared::Game::FindDLCByName(name)) {
                    result |= KCDC::Shared::Game::DLCBit(dlc->id);
                }
            }
            return result;
        }

        /** The level's own station or a placed one, unless a level edit has it out of the player's world. */
        const KCDC::Shared::Game::CraftStation *FindStation(std::uint64_t guid, const std::string &level, KCDC::Shared::Game::CraftKind kind, MafiaNet::VirtualWorldId virtualWorld) {
            const auto *station = KCDC::Features::SmartObject::SmartObjectService::Get().FindCraftStation(guid, kind, virtualWorld);
            return station != nullptr && station->level == level ? station : nullptr;
        }

        /** Base XP on one skill's track; the owner applies its own multipliers. */
        void AddSkillXp(std::uint64_t playerId, std::uint32_t skill, double xp) {
            auto *human     = KCDC::Features::Player::Players::Find(playerId);
            const int track = KCDC::Shared::Game::FindProgressionTrack(KCDC::Shared::Game::ProgressionTrackKind::Skill, skill);
            if (human != nullptr && track >= 0) {
                Server::Get()->GetProgression().AddXp(human, static_cast<std::uint8_t>(track), xp, true);
            }
        }
    } // namespace

    CraftingService::CraftingService(): _alchemy(Hooks()), _smithing(SmithingHooksFor()) {}

    std::uint64_t CraftingService::NowMs() {
        return MafiaNet::GetTimeUS() / 1000;
    }

    AlchemyHooks CraftingService::Hooks() {
        AlchemyHooks hooks;
        hooks.inventory = [](std::uint64_t playerId) {
            return Server::Get()->GetInventory().Find(playerId);
        };
        hooks.starting = [](std::uint64_t playerId, const nlohmann::json &proposal) {
            return KCDC::Scripting::CraftingStarting(playerId, proposal);
        };
        hooks.completing = [](std::uint64_t playerId, const nlohmann::json &proposal) {
            return KCDC::Scripting::CraftingCompleting(playerId, proposal);
        };
        hooks.refunding = [](std::uint64_t playerId, const nlohmann::json &proposal) {
            return KCDC::Scripting::CraftingRefunding(playerId, proposal);
        };
        hooks.experience = [](std::uint64_t playerId, double xp) {
            AddSkillXp(playerId, KCDC::Shared::Crafting::AlchemyNative::kSkill, xp);
        };
        return hooks;
    }

    SmithingHooks CraftingService::SmithingHooksFor() {
        SmithingHooks hooks;
        hooks.inventory = [](std::uint64_t playerId) {
            return Server::Get()->GetInventory().Find(playerId);
        };
        hooks.starting = [](std::uint64_t playerId, const nlohmann::json &proposal) {
            return KCDC::Scripting::CraftingStarting(playerId, proposal);
        };
        hooks.completing = [](std::uint64_t playerId, const nlohmann::json &proposal) {
            return KCDC::Scripting::CraftingCompleting(playerId, proposal);
        };
        hooks.refunding = [](std::uint64_t playerId, const nlohmann::json &proposal) {
            return KCDC::Scripting::CraftingRefunding(playerId, proposal);
        };
        hooks.experience = [](std::uint64_t playerId, double xp) {
            AddSkillXp(playerId, KCDC::Shared::Crafting::SmithingNative::kSkill, xp);
        };
        hooks.equipped = [](std::uint64_t playerId) {
            return Server::Get()->GetInventory().Equipped(playerId);
        };
        hooks.busy = [](std::uint64_t playerId) {
            return Server::Get()->GetCrafting().HoldsAlchemy(playerId);
        };
        return hooks;
    }

    void CraftingService::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::AlchemyRequest>([this](const KCDC::Shared::RPC::AlchemyRequest &request, MafiaNet::Packet *packet) {
            if (packet != nullptr && request.valid) {
                Receive(MafiaNet::ToPeerGuid(packet->guid), request.body);
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::SmithingBegin>([this](const KCDC::Shared::RPC::SmithingBegin &request, MafiaNet::Packet *packet) {
            if (packet != nullptr && request.IsSane()) {
                ReceiveSmithing({MafiaNet::ToPeerGuid(packet->guid), request, std::nullopt});
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::SmithingFinish>([this](const KCDC::Shared::RPC::SmithingFinish &report, MafiaNet::Packet *packet) {
            if (packet != nullptr && report.IsSane()) {
                ReceiveSmithing({MafiaNet::ToPeerGuid(packet->guid), std::nullopt, report});
            }
        });
    }

    void CraftingService::ReceiveSmithing(IncomingSmithing message) {
        std::scoped_lock lock(_incomingMutex);
        const auto pending = std::count_if(_incomingSmithing.begin(), _incomingSmithing.end(), [&message](const IncomingSmithing &queued) {
            return queued.peer == message.peer;
        });
        if (_incomingSmithing.size() < kMaximumQueuedRequests && static_cast<std::size_t>(pending) < kMaximumQueuedPerPeer) {
            _incomingSmithing.push_back(std::move(message));
        }
    }

    void CraftingService::Receive(MafiaNet::PeerGuid peer, const std::string &body) {
        if (body.empty() || body.size() > KCDC::Shared::RPC::AlchemyRequest::kMaximumBytes) {
            return;
        }
        std::scoped_lock lock(_incomingMutex);
        const auto pending = std::count_if(_incoming.begin(), _incoming.end(), [peer](const Incoming &request) {
            return request.peer == peer;
        });
        if (_incoming.size() < kMaximumQueuedRequests && static_cast<std::size_t>(pending) < kMaximumQueuedPerPeer) {
            _incoming.push_back({peer, body});
        }
    }

    std::optional<AlchemyActor> CraftingService::Actor(std::uint64_t playerId) const {
        const auto *human = KCDC::Features::Player::Players::Find(playerId);
        if (human == nullptr) {
            return std::nullopt;
        }
        AlchemyActor actor;
        actor.playerId     = playerId;
        actor.nowMs        = NowMs();
        actor.position     = human->position;
        actor.virtualWorld = human->GetVirtualWorld();
        actor.level        = Server::Get()->GetModConfig().Get<std::string>("level");
        actor.requiredDLC  = RequiredDLC();
        actor.canAct       = human->soul.CanAct();
        return actor;
    }

    bool CraftingService::Snapshot(std::uint64_t playerId, KCDC::Features::Player::SoulSnapshot &soul, std::string &error) const {
        const auto *human = KCDC::Features::Player::Players::Find(playerId);
        if (human == nullptr) {
            error = "playerNotConnected";
            return false;
        }
        // Skills and perks are the owner's, checked by the progression
        // module; nothing before the first accepted digest can stand in.
        const auto *digest = Server::Get()->GetProgression().Accepted(playerId);
        if (digest == nullptr) {
            error = "progressionUnavailable";
            return false;
        }
        soul       = {};
        soul.clock = {NowMs(), Server::Get()->GetWorld().GetWorldTimeMilliseconds()};
        for (std::size_t track = 0; track < KCDC::Shared::Game::g_ProgressionTracks.size() && track < KCDC::Shared::RPC::kProgressionTracks; ++track) {
            if (KCDC::Shared::Game::g_ProgressionTracks[track].kind == KCDC::Shared::Game::ProgressionTrackKind::Skill) {
                soul.profile.skillLevels[KCDC::Shared::Game::g_ProgressionTracks[track].native] = digest->tracks[track].level;
            }
        }
        for (std::size_t perk = 0; perk < KCDC::Shared::Game::g_Perks.size(); ++perk) {
            if (digest->perks.Has(perk)) {
                soul.profile.perks.insert(std::string(KCDC::Shared::Game::g_Perks[perk].guid));
            }
        }
        // A perk's own buff is a source only through the perk: reported
        // again it would count twice, and reported without the perk it is
        // one the accepted progression does not grant.
        std::set<std::string> perkBuffs;
        for (const auto &edge : KCDC::Shared::Game::g_PerkBuffRules) {
            perkBuffs.insert(std::string(edge.buff));
        }
        if (const auto *report = Server::Get()->GetBuffs().Report(playerId)) {
            std::set<std::string> seen;
            for (const auto &observed : *report) {
                const auto *entry = KCDC::Shared::Game::FindBuffById(observed.buff.buff);
                // Only permanent buffs: a timed one would need a simulation
                // of its remaining time the server does not run.
                if (entry == nullptr || entry->duration >= 0.0F) {
                    continue;
                }
                const std::string guid(entry->guid);
                if (perkBuffs.contains(guid) || !seen.insert(guid).second) {
                    continue;
                }
                soul.profile.effects.push_back({std::to_string(observed.buff.handle), guid, std::nullopt});
            }
        }
        soul.profile.health = human->soul.valid ? human->soul.resources[KCDC::Shared::Game::HumanSoulState::kHealth].current : 0.0F;
        return true;
    }

    bool CraftingService::Evaluate(std::uint64_t playerId, AlchemyCharacterContext &context, std::string &error) const {
        KCDC::Features::Player::SoulSnapshot soul;
        return Snapshot(playerId, soul, error) && AlchemyCharacterEvaluator::Evaluate(soul, context, error);
    }

    std::optional<SmithingActor> CraftingService::SmithingActorOf(std::uint64_t playerId) const {
        const auto alchemy = Actor(playerId);
        if (!alchemy) {
            return std::nullopt;
        }
        SmithingActor actor;
        actor.playerId     = playerId;
        actor.nowMs        = alchemy->nowMs;
        actor.position     = alchemy->position;
        actor.virtualWorld = alchemy->virtualWorld;
        actor.level        = alchemy->level;
        actor.requiredDLC  = alchemy->requiredDLC;
        actor.canAct       = alchemy->canAct;
        return actor;
    }

    void CraftingService::PushSmithingClosed(std::uint64_t playerId, std::uint32_t session, KCDC::Shared::RPC::SmithingCloseReason reason) const {
        const auto *human = KCDC::Features::Player::Players::Find(playerId);
        if (human == nullptr) {
            return;
        }
        KCDC::Shared::RPC::SmithingClosed message;
        message.session = session;
        message.reason  = static_cast<std::uint8_t>(reason);
        Framework::CoreModules::GetNetworkPeer()->SendRPC(message, MafiaNet::ToGuid(human->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void CraftingService::HandleSmithing(const IncomingSmithing &message) {
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(message.peer);
        if (human == nullptr) {
            return;
        }
        const auto playerId = human->GetNetworkID();
        auto actor          = SmithingActorOf(playerId);
        if (!actor) {
            return;
        }
        if (message.finish) {
            const auto reply = _smithing.Finish(*actor, message.finish->session, static_cast<KCDC::Shared::RPC::SmithingOutcome>(message.finish->outcome), message.finish->quality);
            if (reply.code != KCDC::Shared::RPC::SmithingCode::Accepted) {
                Framework::Logging::GetLogger(kLogger)->debug("Player {} smithing workpiece {} settled with code {}", playerId, message.finish->session, static_cast<int>(reply.code));
            }
            DispatchEvents();
            return;
        }
        KCDC::Features::Player::SoulSnapshot soul;
        SmithingCharacterContext character;
        std::string error;
        if (Snapshot(playerId, soul, error) && SmithingEvaluator::Evaluate(soul, character, error)) {
            actor->character = std::move(character);
        }
        else {
            Framework::Logging::GetLogger(kLogger)->debug("Player {} smithing character unavailable: {}", playerId, error);
        }
        const auto *station = FindStation(message.begin->station, actor->level, KCDC::Shared::Game::CraftKind::Smithing, human->GetVirtualWorld());
        const auto reply    = station == nullptr ? SmithingReply {KCDC::Shared::RPC::SmithingCode::UnknownStation, 0} : _smithing.Begin(*actor, std::string(station->id), message.begin->recipe);
        // A veto handler can disconnect the player; answer only a connection that is still theirs.
        if (Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(message.peer) == human) {
            KCDC::Shared::RPC::SmithingVerdict verdict;
            verdict.request = message.begin->request;
            verdict.session = reply.session;
            verdict.code    = static_cast<std::uint8_t>(reply.code);
            Framework::CoreModules::GetNetworkPeer()->SendRPC(verdict, MafiaNet::ToGuid(message.peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        DispatchEvents();
    }

    nlohmann::json CraftingService::CharacterView(std::uint64_t playerId, const AlchemyCharacterContext &context) const {
        auto view = context;
        // C_Alchemy keeps the bonus it captured on entry for the whole visit.
        if (const auto bonus = _alchemy.EntryBonus(playerId)) {
            view.sessionQualityBonus = *bonus;
        }
        return AlchemyCharacterEvaluator::EncodeProjection(view, _alchemy.Knowledge(playerId));
    }

    nlohmann::json CraftingService::Response(std::uint64_t playerId, const std::string &request, const std::string &op, const AlchemyReply &reply) const {
        const auto *inventory   = Server::Get()->GetInventory().Find(playerId);
        nlohmann::json response = {{"request", request}, {"op", op}, {"ok", reply.ok}, {"code", reply.code}, {"session", reply.session}, {"sequence", std::to_string(reply.sequence)}, {"inventoryRevision", inventory == nullptr ? 0 : inventory->Revision()}};
        if (!reply.state.is_null()) {
            response["state"] = reply.state;
        }
        return response;
    }

    void CraftingService::Send(MafiaNet::PeerGuid peer, const nlohmann::json &response) const {
        KCDC::Shared::RPC::AlchemyResponse message;
        message.body = response.dump();
        if (message.body.size() > KCDC::Shared::RPC::AlchemyResponse::kMaximumBytes) {
            Framework::Logging::GetLogger(kLogger)->error("Alchemy response of {} bytes dropped", message.body.size());
            return;
        }
        Framework::CoreModules::GetNetworkPeer()->SendRPC(message, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void CraftingService::Push(std::uint64_t playerId, const std::string &op, const std::string &reason) {
        const auto *human = KCDC::Features::Player::Players::Find(playerId);
        if (human == nullptr) {
            return;
        }
        auto response = Response(playerId, "", op, _alchemy.Current(playerId));
        if (op == "closed") {
            response["reason"] = reason;
        }
        if (op == "character") {
            // Before the first progression digest there is no character to
            // send; the next table response carries the knowledge instead.
            AlchemyCharacterContext character;
            std::string error;
            if (!Evaluate(playerId, character, error)) {
                return;
            }
            response["character"] = CharacterView(playerId, character);
        }
        Send(human->ownerGUID, response);
    }

    void CraftingService::Handle(MafiaNet::PeerGuid peer, const nlohmann::json &request) {
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer);
        if (human == nullptr) {
            return;
        }
        const auto playerId = human->GetNetworkID();
        auto actor          = Actor(playerId);
        if (!actor) {
            return;
        }
        const auto op      = request.at("op").get<std::string>();
        const auto token   = request.at("request").get<std::string>();
        const auto session = request.value("session", std::string());
        std::uint64_t sequence = 0;
        KCDC::Shared::Crafting::ReadSequence(request, sequence);
        AlchemyCharacterContext character;
        std::string error;
        const bool evaluated = Evaluate(playerId, character, error);
        if (evaluated) {
            actor->character = character;
        }
        else {
            Framework::Logging::GetLogger(kLogger)->debug("Player {} alchemy character unavailable: {}", playerId, error);
        }

        AlchemyReply reply;
        if (op == "inspect") {
            reply      = _alchemy.Current(playerId);
            reply.ok   = evaluated;
            reply.code = evaluated ? "" : "alchemyCharacterUnavailable";
        }
        else if (op == "start") {
            std::uint64_t guid = 0;
            KCDC::Shared::Crafting::ReadIdentity(request, "station", guid);
            const auto *station = FindStation(guid, actor->level, KCDC::Shared::Game::CraftKind::Alchemy, human->GetVirtualWorld());
            if (station == nullptr || _smithing.SessionOf(playerId)) {
                // One craft at a time: a workpiece on the anvil holds the player.
                reply      = _alchemy.Current(playerId);
                reply.ok   = false;
                reply.code = station == nullptr ? "unknownStation" : "playerBusy";
            }
            else {
                reply = _alchemy.Start(*actor, token, std::string(station->id), request.value("continuationOf", std::string()));
            }
        }
        else if (op == "entryready") {
            reply = _alchemy.EntryReady(*actor, session, sequence);
        }
        else if (op == "action") {
            reply = _alchemy.Select(*actor, session, sequence, request.at("target").get<std::uint32_t>(), request.value("itemId", std::string()));
        }
        else if (op == "return") {
            reply = _alchemy.Return(*actor, session, sequence);
        }
        else if (op == "pulse") {
            reply = _alchemy.Pulse(*actor, session, sequence);
        }
        else if (op == "complete") {
            reply = _alchemy.Complete(*actor, session, sequence);
        }
        else if (op == "cancel") {
            reply = _alchemy.Cancel(*actor, session, request.value("startRequest", std::string()), request.value("continuationOf", std::string()), request.value("reason", std::string("cancelled")));
        }
        else {
            return;
        }

        // A veto handler can disconnect the player; answer only a connection that is still theirs.
        if (Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer) != human) {
            DispatchEvents();
            return;
        }
        auto response = Response(playerId, token, op, reply);
        if (evaluated && (op == "inspect" || op == "start" || op == "entryready" || op == "complete")) {
            response["character"] = CharacterView(playerId, character);
        }
        // Native animation can legitimately beat the server's lower bound by
        // network and scheduling jitter; say how long to wait.
        if (reply.code == "alchemyActionTooEarly" && reply.state.is_object()) {
            response["retryAfterMs"] = std::max<std::uint64_t>(reply.state.at("readyAtMs").get<std::uint64_t>() > actor->nowMs ? reply.state.at("readyAtMs").get<std::uint64_t>() - actor->nowMs : 0, 1);
        }
        Send(peer, response);
        DispatchEvents();
    }

    void CraftingService::DispatchEvents() {
        for (const auto &lost : _smithing.TakeUnreturned()) {
            Framework::Logging::GetLogger(kLogger)->error("Player {} could not get {} of {} back from smithing: {}", lost.playerId, lost.amount, lost.itemClass, lost.code);
        }
        auto events   = _alchemy.TakeEvents();
        auto smithing = _smithing.TakeEvents();
        if (events.empty() && smithing.empty()) {
            return;
        }
        Server::Get()->GetInventory().DispatchChanges();
        for (const auto &event : events) {
            KCDC::Scripting::EmitCraftingEvent(event.name, event.playerId, event.payload);
        }
        for (const auto &event : smithing) {
            KCDC::Scripting::EmitCraftingEvent(event.name, event.playerId, event.payload);
        }
    }

    void CraftingService::Update() {
        std::vector<Incoming> incoming;
        std::vector<IncomingSmithing> smithing;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_incoming);
            smithing.swap(_incomingSmithing);
        }
        for (const auto &message : incoming) {
            nlohmann::json request;
            std::string error;
            if (KCDC::Shared::Crafting::ReadRequest(message.body, request, error)) {
                Handle(message.peer, request);
            }
        }
        for (const auto &message : smithing) {
            HandleSmithing(message);
        }
        const auto closed = _alchemy.Expire(NowMs(), [this](std::uint64_t playerId) {
            return Actor(playerId);
        });
        for (const auto &[playerId, reason] : closed) {
            Push(playerId, "closed", reason);
        }
        const auto expiring = _smithing.Expire(NowMs(), [this](std::uint64_t playerId) {
            return SmithingActorOf(playerId);
        });
        for (const auto &closure : expiring) {
            PushSmithingClosed(closure.playerId, closure.session, closure.reason);
        }
        DispatchEvents();
    }

    void CraftingService::Reset() {
        _alchemy  = AlchemyModule(Hooks());
        _smithing = SmithingModule(SmithingHooksFor());
        std::scoped_lock lock(_incomingMutex);
        _incoming.clear();
        _incomingSmithing.clear();
    }

    void CraftingService::OnPlayerConnect(MafiaNet::PeerGuid peer) {
        _recipesSent[peer] = 0;
        PublishRecipes();
    }

    void CraftingService::PublishRecipes() {
        const std::size_t registered = KCDC::Shared::Game::CustomRecipeRegistry::Get().Registered().size();
        for (auto &[peer, sent] : _recipesSent) {
            if (sent < registered) {
                SendRecipes(peer, sent);
                sent = registered;
            }
        }
    }

    void CraftingService::SendRecipes(MafiaNet::PeerGuid peer, std::size_t from) const {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        const auto &recipes = KCDC::Shared::Game::CustomRecipeRegistry::Get().Registered();
        const auto send     = [&](std::size_t first, const nlohmann::json &batch) {
            KCDC::Shared::RPC::CustomRecipeDefinitions message;
            message.body = nlohmann::json {{"from", first}, {"recipes", batch}}.dump();
            net->SendRPC(message, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        };
        // Filled by size, never by count: a message past the envelope's limit would not be sent at
        // all, and the client would never see the recipes behind it.
        nlohmann::json batch = nlohmann::json::array();
        std::size_t first    = from;
        std::size_t bytes    = 0;
        for (std::size_t index = from; index < recipes.size(); ++index) {
            nlohmann::json definition = KCDC::Shared::Game::CustomRecipeToJson(recipes[index]->Definition());
            const std::size_t size    = definition.dump().size();
            if (!batch.empty() && bytes + size > KCDC::Shared::RPC::CustomRecipeDefinitions::kBudgetBytes) {
                send(first, batch);
                batch = nlohmann::json::array();
                first = index;
                bytes = 0;
            }
            batch.push_back(std::move(definition));
            bytes += size;
        }
        if (!batch.empty()) {
            send(first, batch);
        }
    }

    void CraftingService::OnPlayerDisconnect(MafiaNet::PeerGuid peer) {
        _recipesSent.erase(peer);
        {
            std::scoped_lock lock(_incomingMutex);
            std::erase_if(_incoming, [peer](const Incoming &request) {
                return request.peer == peer;
            });
            std::erase_if(_incomingSmithing, [peer](const IncomingSmithing &request) {
                return request.peer == peer;
            });
        }
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(peer);
        if (human == nullptr) {
            return;
        }
        _alchemy.End(human->GetNetworkID(), "disconnected");
        _smithing.End(human->GetNetworkID(), "disconnected");
        DispatchEvents();
    }

    void CraftingService::OnPlayerLeft(std::uint64_t playerId) {
        _alchemy.Forget(playerId);
    }

    nlohmann::json CraftingService::Session(std::uint64_t playerId) const {
        auto alchemy = _alchemy.Inspect(playerId, NowMs());
        if (!alchemy.is_null()) {
            alchemy["kind"] = "alchemy";
            return alchemy;
        }
        return _smithing.Inspect(playerId, NowMs());
    }

    nlohmann::json CraftingService::Occupancy(const std::string &station, std::uint32_t virtualWorld) const {
        auto alchemy = _alchemy.Occupancy(station, virtualWorld, NowMs());
        return alchemy.is_null() ? _smithing.Occupancy(station, virtualWorld, NowMs()) : alchemy;
    }

    bool CraftingService::HoldsAlchemy(std::uint64_t playerId) const {
        return !_alchemy.Inspect(playerId, NowMs()).is_null();
    }

    bool CraftingService::Cancel(std::uint64_t playerId) {
        // Events follow on the next tick, never inside the calling script.
        const bool alchemy = _alchemy.End(playerId, "cancelled");
        if (alchemy) {
            Push(playerId, "closed", "cancelled");
        }
        const auto workpiece = _smithing.SessionOf(playerId);
        const bool smithing  = workpiece && _smithing.End(playerId, "cancelled");
        if (smithing) {
            PushSmithingClosed(playerId, *workpiece, KCDC::Shared::RPC::SmithingCloseReason::Cancelled);
        }
        return alchemy || smithing;
    }

    nlohmann::json CraftingService::Knowledge(std::uint64_t playerId) const {
        return _alchemy.Knowledge(playerId);
    }

    bool CraftingService::SetKnowledge(std::uint64_t playerId, const std::string &recipe, std::uint32_t mask) {
        if (KCDC::Features::Player::Players::Find(playerId) == nullptr || !_alchemy.SetKnowledge(playerId, recipe, mask)) {
            return false;
        }
        Push(playerId, "character");
        return true;
    }
} // namespace KCDC::Features::Crafting
