#include "features/player/player_buff_module.h"

#include "features/player/player_buff_scripting.h"
#include "features/player/player_roster.h"
#include "features/player/player_scripting.h"

#include "shared/features/player/player_custom_buff.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/body_part_catalog.h"
#include "shared/game/custom_buff_registry.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kLogger = "Buffs";

        const Shared::RPC::ObservedBuff *Find(const std::vector<Shared::RPC::ObservedBuff> &active, const Shared::RPC::BuffId &buff) {
            for (const auto &entry : active) {
                if (entry.buff.buff == buff) {
                    return &entry;
                }
            }
            return nullptr;
        }

        /** The name a script would recognise, or the placeholder for a row we have none for. */
        std::string NameOf(const Shared::RPC::BuffId &buff) {
            const std::string_view name = Shared::Game::BuffNameOf(buff);
            return name.empty() ? std::string("<unknown buff>") : std::string(name);
        }
    } // namespace

    void Buffs::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<Shared::RPC::BuffReport>([this](const Shared::RPC::BuffReport &report, MafiaNet::Packet *packet) {
            auto *human = report.IsSane() ? Players::FromPacket(packet) : nullptr;
            if (human == nullptr) {
                return;
            }
            AcceptReport(human->GetNetworkID(), human->ownerGUID, report);
        });

        net->RegisterRPC<Shared::RPC::BuffRefused>([](const Shared::RPC::BuffRefused &refused, MafiaNet::Packet *packet) {
            auto *human = refused.IsSane() ? Players::FromPacket(packet) : nullptr;
            if (human == nullptr) {
                return;
            }
            Scripting::Events::PlayerBuffBlocked(human->GetNetworkID(), NameOf(refused.buff));
        });
    }

    void Buffs::OnPlayerConnect(MafiaNet::PeerGuid guid) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::CustomBuffDefinition reset;
        reset.body = "{\"reset\":true}";
        net->SendRPC(reset, MafiaNet::ToGuid(guid));
        for (const auto &[id, definition] : Shared::Game::CustomBuffRegistry::Get().Snapshot()) {
            Shared::RPC::CustomBuffDefinition message;
            message.body = nlohmann::json {{"id", id}, {"definition", definition}}.dump();
            net->SendRPC(message, MafiaNet::ToGuid(guid));
        }
        Shared::RPC::BuffPolicy policy;
        policy.claimedClasses = _claimedClasses;
        net->SendRPC(policy, MafiaNet::ToGuid(guid));
    }

    void Buffs::OnPlayerDisconnect(MafiaNet::PeerGuid, std::uint64_t playerNetworkId) {
        std::vector<Shared::RPC::ObservedBuff> active;
        if (const auto *state = FindPlayer(playerNetworkId)) {
            active = state->active;
        }
        _players.erase(playerNetworkId);
        for (const auto &entry : active) {
            Scripting::Events::PlayerCustomBuff(playerNetworkId, NameOf(entry.buff.buff), "playerCustomBuffRemove", "disconnect");
        }
    }

    void Buffs::PublishDefinition(const std::string &id) {
        const auto *definition = Shared::Game::CustomBuffRegistry::Get().Find(id);
        auto *net              = Framework::CoreModules::GetNetworkPeer();
        if (definition == nullptr || net == nullptr) {
            return;
        }
        Shared::RPC::CustomBuffDefinition message;
        message.body = nlohmann::json {{"id", definition->id}, {"definition", definition->definition}}.dump();
        net->BroadcastRPC(message);
    }

    void Buffs::Update() {
        const auto &registry = Shared::Game::CustomBuffRegistry::Get();
        if (!registry.HasTicking()) {
            return;
        }
        std::vector<std::pair<std::uint64_t, Shared::RPC::ObservedBuff>> ticks;
        const auto now = MafiaNet::GetTime();
        for (auto &[playerId, state] : _players) {
            for (const auto &entry : state.active) {
                // The registry is keyed by GUID: one map lookup rather than a
                // scan of the native table by name.
                const auto *definition = registry.Find(Shared::Game::CryGuidToString(entry.buff.buff.low, entry.buff.buff.high));
                if (definition == nullptr || definition->tickInterval == 0) {
                    continue;
                }
                const auto interval = static_cast<std::uint64_t>(definition->tickInterval * 1000);
                auto &next          = state.nextTicks[entry.buff.handle];
                if (next == 0) {
                    next = now + interval;
                }
                if (now < next) {
                    continue;
                }
                next = now + interval;
                ticks.emplace_back(playerId, entry);
            }
        }
        // Callbacks can disconnect players or remove effects. Hold values,
        // and recheck membership without retaining a map iterator in script.
        for (const auto &[playerId, entry] : ticks) {
            const auto *state = FindPlayer(playerId);
            if (state == nullptr
                || std::none_of(state->active.begin(), state->active.end(),
                    [&entry](const auto &live) {
                        return live.buff.handle == entry.buff.handle;
                    })
                || std::find(state->pendingRemovals.begin(), state->pendingRemovals.end(), entry.buff.buff) != state->pendingRemovals.end()) {
                continue;
            }
            Scripting::Events::PlayerCustomBuff(playerId, NameOf(entry.buff.buff), "playerCustomBuffTick");
        }
    }

    void Buffs::ClaimClasses(std::uint32_t mask) {
        _claimedClasses = mask;

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::BuffPolicy policy;
        policy.claimedClasses = mask;
        net->BroadcastRPC(policy);
        Framework::Logging::GetLogger(kLogger)->info("Claiming buff classes 0x{:08X}", mask);
    }

    bool Buffs::Apply(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff, std::uint32_t incarnation) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::ApplyBuff order;
        order.buff        = Shared::Game::BuffIdOf(buff);
        order.incarnation = incarnation;
        if (!order.IsSane()) {
            return false;
        }

        PlayerBuffs &state = _players[player->GetNetworkID()];
        state.owner        = player->ownerGUID;

        // Otherwise a removal stays pending against a definition coming back.
        std::erase(state.pendingRemovals, order.buff);

        net->SendRPC(order, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    bool Buffs::Retract(Shared::Entities::HumanEntity *player, const Shared::Game::BuffEntry &buff) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::RetractBuff order;
        order.buff = Shared::Game::BuffIdOf(buff);
        if (!order.IsSane()) {
            return false;
        }

        PlayerBuffs &state = _players[player->GetNetworkID()];
        state.owner        = player->ownerGUID;

        // Naming the instance is better than naming the definition: the
        // definition form removes every match, and a merged stack is one
        // instance carrying several drinks' worth.
        if (const Shared::RPC::ObservedBuff *live = Find(state.active, order.buff); live != nullptr && Shared::Game::CustomBuffRegistry::Get().Find(buff.guid) == nullptr) {
            order.handle = live->buff.handle;
        }
        if (std::find(state.pendingRemovals.begin(), state.pendingRemovals.end(), order.buff) == state.pendingRemovals.end()) {
            state.pendingRemovals.push_back(order.buff);
        }

        net->SendRPC(order, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    bool Buffs::ClearByTag(Shared::Entities::HumanEntity *player, int aiTagId) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }

        Shared::RPC::ClearBuffsByTag order;
        order.aiTag = aiTagId;
        if (!order.IsSane()) {
            return false;
        }

        // Deliberately not recorded as a pending removal: a tag names a family
        // rather than a definition, so what it takes off is reported as an
        // expiry.
        net->SendRPC(order, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    const std::vector<Shared::RPC::ObservedBuff> *Buffs::Report(std::uint64_t playerNetworkId) const {
        const PlayerBuffs *state = FindPlayer(playerNetworkId);
        return state != nullptr && state->revision != 0 ? &state->active : nullptr;
    }

    bool Buffs::Has(std::uint64_t playerNetworkId, const Shared::RPC::BuffId &buff) const {
        const PlayerBuffs *state = FindPlayer(playerNetworkId);
        return state != nullptr && Find(state->active, buff) != nullptr;
    }

    void Buffs::AcceptReport(std::uint64_t playerNetworkId, MafiaNet::PeerGuid guid, const Shared::RPC::BuffReport &report) {
        PlayerBuffs &state = _players[playerNetworkId];
        state.owner        = guid;

        // Wrap-safe "came after", so a report overtaken in flight is dropped
        // rather than winding the body's state backwards.
        if (state.revision != 0 && static_cast<std::int32_t>(report.revision - state.revision) <= 0) {
            return;
        }

        const std::vector<Shared::RPC::ObservedBuff> previous = state.active;
        const std::uint64_t now                               = MafiaNet::GetTime();
        state.revision                                        = report.revision;

        // A handle the previous report carried keeps the moment this server
        // first saw it. Measured here rather than taken off the wire: the
        // owner's clock is not this one.
        state.active.clear();
        state.active.reserve(report.active.size());
        for (const Shared::RPC::ActiveBuff &entry : report.active) {
            const auto seen = std::find_if(previous.begin(), previous.end(), [&entry](const Shared::RPC::ObservedBuff &observed) {
                return observed.buff.handle == entry.handle;
            });
            state.active.push_back({entry, seen != previous.end() ? seen->firstSeen : now});
        }

        const auto current         = state.active;
        const auto orderedRemovals = state.pendingRemovals;
        std::erase_if(state.nextTicks, [&current](const auto &tick) {
            return std::none_of(current.begin(), current.end(), [&tick](const auto &entry) {
                return tick.first == entry.buff.handle;
            });
        });
        std::erase_if(state.pendingRemovals, [&current](const Shared::RPC::BuffId &buff) {
            return Find(current, buff) == nullptr;
        });

        std::vector<Shared::RPC::BuffId> added;
        for (const auto &entry : current) {
            if (Find(previous, entry.buff.buff) == nullptr && std::find(added.begin(), added.end(), entry.buff.buff) == added.end()) {
                added.push_back(entry.buff.buff);
                Scripting::Events::PlayerBuffAdded(playerNetworkId, NameOf(entry.buff.buff), entry.buff.source == Shared::RPC::BuffSource::Server);
                if (const Shared::Game::BodyPartEntry *limb = Shared::Game::FindHumanLimbByInjuryBuff(entry.buff.buff)) {
                    Scripting::Events::PlayerInjured(playerNetworkId, limb->id);
                }
            }
        }
        std::vector<Shared::RPC::BuffId> removed;
        for (const auto &entry : previous) {
            if (Find(current, entry.buff.buff) != nullptr || std::find(removed.begin(), removed.end(), entry.buff.buff) != removed.end()) {
                continue;
            }
            removed.push_back(entry.buff.buff);
            const bool ordered = std::find(orderedRemovals.begin(), orderedRemovals.end(), entry.buff.buff) != orderedRemovals.end();
            Scripting::Events::PlayerBuffRemoved(playerNetworkId, NameOf(entry.buff.buff), ordered ? "server" : "expired");
            if (const Shared::Game::BodyPartEntry *limb = Shared::Game::FindHumanLimbByInjuryBuff(entry.buff.buff)) {
                Scripting::Events::PlayerInjuryHealed(playerNetworkId, limb->id);
            }
        }

        for (const auto &entry : previous) {
            if (std::any_of(current.begin(), current.end(), [&entry](const auto &live) {
                    return live.buff.handle == entry.buff.handle;
                })) {
                continue;
            }
            const bool ordered = std::find(orderedRemovals.begin(), orderedRemovals.end(), entry.buff.buff) != orderedRemovals.end();
            Scripting::Events::PlayerCustomBuff(playerNetworkId, NameOf(entry.buff.buff), "playerCustomBuffRemove", ordered ? "server" : Find(current, entry.buff.buff) != nullptr ? "refreshed" : "expired");
        }
        for (const auto &entry : current) {
            if (std::any_of(previous.begin(), previous.end(), [&entry](const auto &live) {
                    return live.buff.handle == entry.buff.handle;
                })) {
                continue;
            }
            if (FindPlayer(playerNetworkId) != nullptr) {
                Scripting::Events::PlayerCustomBuff(playerNetworkId, NameOf(entry.buff.buff), "playerCustomBuffApply");
            }
        }
    }

    Buffs::PlayerBuffs *Buffs::FindPlayer(std::uint64_t playerNetworkId) {
        const auto found = _players.find(playerNetworkId);
        return found != _players.end() ? &found->second : nullptr;
    }

    const Buffs::PlayerBuffs *Buffs::FindPlayer(std::uint64_t playerNetworkId) const {
        const auto found = _players.find(playerNetworkId);
        return found != _players.end() ? &found->second : nullptr;
    }
} // namespace KCDC::Features::Player
