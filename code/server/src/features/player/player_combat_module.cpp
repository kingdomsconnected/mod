#include "features/player/player_combat_module.h"

#include "features/npc/npc_verbs.h"
#include "features/player/player_combat_blow.h"
#include "features/player/player_combatant.h"
#include "features/player/player_scripting.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/npc/npc_rpc.h"
#include "shared/features/player/player_bindings.h"
#include "shared/features/player/player_combat_cancel.h"
#include "shared/game/body_part_catalog.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kLogger = "Combat";

        const char *SourceName(KCDC::Shared::RPC::CombatHitSource source) {
            return source == KCDC::Shared::RPC::CombatHitSource::Missile ? "missile" : "melee";
        }

        template <typename T>
        void SendToPair(T message, Framework::Networking::Replication::NetworkEntity &first, Framework::Networking::Replication::NetworkEntity &second) {
            auto *net         = Framework::CoreModules::GetNetworkPeer();
            auto *replication = Framework::CoreModules::GetReplication();
            if (net == nullptr || replication == nullptr) {
                return;
            }
            std::vector<MafiaNet::PeerGuid> sent;
            const auto send = [&](MafiaNet::PeerGuid peer) {
                if (peer == MafiaNet::UNASSIGNED_PEER_GUID || std::find(sent.begin(), sent.end(), peer) != sent.end()) {
                    return;
                }
                sent.push_back(peer);
                net->SendRPC(message, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, Framework::Networking::Channel::Presentation);
            };
            send(first.ownerGUID);
            send(second.ownerGUID);
            replication->ForEachStreamingPeer(&first, send);
            replication->ForEachStreamingPeer(&second, send);
        }
    } // namespace

    void Combat::Install(Missile &missiles) {
        _missiles = &missiles;
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Combat referee installation has no network peer");
            return;
        }
        net->RegisterRPC<KCDC::Shared::RPC::NpcCombatReaction>([this](const KCDC::Shared::RPC::NpcCombatReaction &message, MafiaNet::Packet *packet) {
            OnNpcReaction(message.reaction, packet);
        });
        net->RegisterRPC<KCDC::Shared::RPC::CombatHitReport>([this](const KCDC::Shared::RPC::CombatHitReport &report, MafiaNet::Packet *packet) {
            OnHitReport(report, packet);
        });
        net->RegisterRPC<KCDC::Shared::RPC::CombatClaimAnswer>([this](const KCDC::Shared::RPC::CombatClaimAnswer &answer, MafiaNet::Packet *packet) {
            auto *replication = Framework::CoreModules::GetReplication();
            auto *sender      = packet != nullptr && replication != nullptr ? replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(MafiaNet::ToPeerGuid(packet->guid)) : nullptr;
            if (sender != nullptr && answer.IsSane() && _answers.size() < 512) {
                _answers.emplace_back(answer, sender->GetNetworkID());
            }
        });
    }

    void Combat::OnNpcReaction(const KCDC::Shared::RPC::CombatReaction &reaction, MafiaNet::Packet *packet) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *npc         = replication != nullptr ? replication->GetEntity<KCDC::Shared::Entities::NpcEntity>(reaction.actorNetworkId) : nullptr;
        // Only the NPC's simulator speaks for it, and only for its current life and order.
        if (packet == nullptr || npc == nullptr || !reaction.IsSane() || !npc->alive || npc->ownerGUID != MafiaNet::ToPeerGuid(packet->guid) || npc->spawnEpoch != reaction.incarnation || npc->stateEpoch != reaction.epoch || npc->intentRevision != reaction.combatRevision) {
            return;
        }
        if (KCDC::Shared::Game::HasFlag(npc->flags, KCDC::Shared::Game::NpcFlags::Frozen)) {
            return;
        }
        // A swing is admitted only at the opponent the server ordered.
        const bool swing = reaction.kind == KCDC::Shared::RPC::CombatReaction::Attack || reaction.kind == KCDC::Shared::RPC::CombatReaction::Riposte || reaction.kind == KCDC::Shared::RPC::CombatReaction::SyncPerfectBlock;
        if (swing && (npc->intent.kind != KCDC::Shared::Game::NpcIntentKind::Attack || npc->intent.targetNetworkId != reaction.opponentNetworkId)) {
            return;
        }
        const Combatant other = Combatant::Find(reaction.opponentNetworkId);
        if (!other || other.entity == npc || !MafiaNet::VirtualWorldsCanSee(other.entity->GetVirtualWorld(), npc->GetVirtualWorld())) {
            return;
        }
        auto &context = _npcSwingContexts[npc->GetNetworkID()];
        if (context.incarnation != npc->spawnEpoch || context.revision != npc->intentRevision) {
            _ledgers.erase(npc->GetNetworkID());
            context = {npc->spawnEpoch, npc->intentRevision, 0};
        }
        if (reaction.presentationSequence <= context.sequence) {
            return;
        }
        context.sequence = reaction.presentationSequence;
        RelayReaction(reaction, *npc);
    }

    bool Combat::ProcessNpcReport(const KCDC::Shared::RPC::CombatReportQueue::Entry &entry) {
        const auto &report       = entry.report;
        const Combatant attacker = Combatant::Find(report.attackerNetworkId);
        const Combatant victim   = Combatant::Find(entry.sender);
        if (!attacker || !victim || attacker.entity == victim.entity || !MafiaNet::VirtualWorldsCanSee(attacker.entity->GetVirtualWorld(), victim.entity->GetVirtualWorld()) || report.IsClaim()) {
            return true;
        }
        const auto attackerLife = attacker.Incarnation();
        const auto victimLife   = victim.Incarnation();
        if (attackerLife != entry.otherIncarnation || attacker.entity->stateEpoch != entry.otherEpoch || victimLife != report.incarnation || victim.entity->stateEpoch != report.epoch) {
            return true;
        }
        // An NPC victim is reported by its simulator for its current order.
        if (victim.npc != nullptr ? (!victim.npc->alive || victim.npc->intentRevision != report.reporterRevision || victim.npc->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) : !victim.CanAct()) {
            return true;
        }
        const bool missile   = report.Source() == KCDC::Shared::RPC::CombatHitSource::Missile;
        const auto attacking = [&attacker, &report] {
            const auto *npc = attacker.npc;
            return npc->alive && npc->spawnEpoch == report.attackerIncarnation && npc->intentRevision == report.attackerRevision && npc->intent.kind == KCDC::Shared::Game::NpcIntentKind::Attack && npc->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID
                   && !KCDC::Shared::Game::HasFlag(npc->flags, KCDC::Shared::Game::NpcFlags::Frozen);
        };
        if (attacker.npc != nullptr) {
            // An NPC strikes only the target it was ordered at, in the order that swung.
            if (!attacking() || attacker.npc->intent.targetNetworkId != entry.sender) {
                return true;
            }
            const auto context = _npcSwingContexts.find(attacker.entity->GetNetworkID());
            if (!missile && (context == _npcSwingContexts.end() || context->second.incarnation != attacker.npc->spawnEpoch || context->second.revision != attacker.npc->intentRevision)) {
                return false;
            }
        }
        else if (!attacker.CanAct()) {
            return true;
        }
        if ((!missile && glm::distance(attacker.Position(), victim.Position()) > kMaxMeleeReach) || WasCancelled(attacker.entity->GetNetworkID(), victim.entity->GetNetworkID(), entry.receivedAt)) {
            return true;
        }
        const auto now = MafiaNet::GetTime();
        if (missile) {
            if (_missiles == nullptr || !_missiles->ClaimDamage(report.attackerNetworkId, report.shotSequence, entry.sender, now)) {
                return true;
            }
        }
        else {
            const auto ledger = _ledgers.find(attacker.entity->GetNetworkID());
            const auto check  = ledger != _ledgers.end() ? ledger->second.Check(report.attackGeneration, entry.sender, now, kStrikeSlackMs) : KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing;
            if (check == KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing) {
                return false;
            }
            if (check != KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted) {
                return true;
            }
            ledger->second.Strike(report.attackGeneration, entry.sender, now, kStrikeSlackMs);
        }
        // NPC souls are native game data, not HumanSoulState. Trust only the
        // victim's elected simulator for the native amount; never price an NPC
        // as a zero-stat player or accept an observer's/attacker's estimate.
        float loss = std::min(report.healthLoss, victim.npc != nullptr ? KCDC::Shared::RPC::CombatHitReport::kMaxHealthLoss : KCDC::Shared::Game::CombatPricingParams::kMaxDamage);
        if (victim.npc != nullptr) {
            if (KCDC::Shared::Game::HasFlag(victim.npc->flags, KCDC::Shared::Game::NpcFlags::Invulnerable)) {
                loss = 0.0F;
            }
            else if (!report.engineLethal) {
                loss = std::min(loss, std::max(victim.npc->health - 0.01F, 0.0F));
            }
            // Scaling native health can leave a few float ULPs after a lethal hit.
            else if (loss > 0.0F && victim.npc->health - loss <= 0.001F) {
                loss = std::max(loss, victim.npc->health);
            }
        }
        if (victim.human != nullptr) {
            KCDC::Scripting::Events::PlayerHitProposal proposal;
            proposal.missile      = missile;
            proposal.damage       = loss;
            proposal.engineDamage = report.healthLoss;
            proposal.stamina      = report.staminaLoss;
            proposal.reason       = KCDC::Shared::Game::ToDamageReason(report.reason);
            proposal.flags        = report.flags;
            proposal.bodyPart     = report.hasInputs ? KCDC::Shared::Game::BodyPartOfSubpart(report.hitSubpart) : KCDC::Shared::Game::kNoBodyPart;
            const auto victimId   = victim.entity->GetNetworkID();
            const bool allowed    = KCDC::Scripting::Events::PlayerHit(victimId, attacker.entity->GetNetworkID(), proposal);
            // A synchronous handler may remove either body or change the NPC's order.
            if (Combatant::Find(victimId).entity != victim.entity || Combatant::Find(report.attackerNetworkId).entity != attacker.entity) {
                return true;
            }
            if (!victim.CanAct() || victim.Incarnation() != victimLife || victim.entity->stateEpoch != report.epoch) {
                return true;
            }
            if (attacker.npc != nullptr && (!attacking() || attacker.entity->stateEpoch != entry.otherEpoch)) {
                return true;
            }
            if (WasCancelled(report.attackerNetworkId, victimId, entry.receivedAt)) {
                return true;
            }
            loss = allowed && std::isfinite(proposal.damage) ? std::clamp(proposal.damage, 0.0F, KCDC::Shared::RPC::CombatHitReport::kMaxHealthLoss) : 0.0F;
        }
        KCDC::Shared::RPC::CombatOutcome outcome;
        outcome.sequence          = ++_lastRuling;
        outcome.attackerNetworkId = attacker.entity->GetNetworkID();
        outcome.victimNetworkId   = victim.entity->GetNetworkID();
        outcome.victimIncarnation = victimLife;
        outcome.victimEpoch       = victim.entity->stateEpoch;
        outcome.attackGeneration  = report.attackGeneration;
        outcome.shotSequence      = report.shotSequence;
        outcome.source            = report.source;
        outcome.healthLoss        = loss;
        outcome.staminaLoss       = report.staminaLoss;
        outcome.mayKill           = report.engineLethal;
        outcome.reason            = report.reason;
        outcome.flags             = report.flags;
        outcome.recoilRowIndex    = report.recoilRowIndex;
        outcome.hasImpact         = report.hasImpact;
        outcome.impact            = report.impact;
        outcome.ruledAt           = now;
        outcome.reportedAt        = entry.receivedAt;
        SendToPair(outcome, *attacker.entity, *victim.entity);
        if (victim.npc != nullptr) {
            KCDC::Features::Npc::Npcs::Damage(victim.npc, attacker.entity->GetNetworkID(), loss);
        }
        else if (loss > 0.0F) {
            KCDC::Scripting::Events::PlayerDamaged(victim.entity->GetNetworkID(), attacker.entity->GetNetworkID(), loss, KCDC::Shared::Game::kNoBodyPart, KCDC::Shared::Game::ToDamageReason(report.reason));
        }
        return true;
    }

    void Combat::OnPlayerDisconnect(std::uint64_t playerNetworkId) {
        _reports.Forget(playerNetworkId);
        std::erase_if(_cancelledAt, [playerNetworkId](const auto &pair) {
            return pair.first.first == playerNetworkId || pair.first.second == playerNetworkId;
        });
        std::erase_if(_attacks, [playerNetworkId](const auto &notice) {
            return std::get<0>(notice) == playerNetworkId || std::get<1>(notice) == playerNetworkId;
        });
        _ledgers.erase(playerNetworkId);
        std::erase_if(_claims, [playerNetworkId](const auto &claim) {
            return std::get<0>(claim.first) == playerNetworkId || std::get<2>(claim.first) == playerNetworkId;
        });
    }

    bool Combat::WasCancelled(std::uint64_t player, std::uint64_t opponent, std::uint64_t receivedAt) const {
        const auto found = _cancelledAt.find(std::minmax(player, opponent));
        return found != _cancelledAt.end() && receivedAt <= found->second;
    }

    bool Combat::Cancel(std::uint64_t player, std::uint64_t opponent) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *first       = replication != nullptr ? replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(player) : nullptr;
        auto *second      = replication != nullptr ? replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(opponent) : nullptr;
        if (first == nullptr || second == nullptr || first == second || first->GetVirtualWorld() != second->GetVirtualWorld()) {
            return false;
        }
        const auto now                              = MafiaNet::GetTime();
        _cancelledAt[std::minmax(player, opponent)] = now;
        _reports.Forget(player, opponent);
        std::erase_if(_claims, [player, opponent](const auto &entry) {
            const auto &report = entry.second.report;
            return (report.attackerNetworkId == player && report.victimNetworkId == opponent) || (report.attackerNetworkId == opponent && report.victimNetworkId == player);
        });
        _ledgers[player].CancelAgainst(opponent);
        _ledgers[opponent].CancelAgainst(player);
        KCDC::Shared::RPC::CombatCancel message;
        message.first             = player;
        message.second            = opponent;
        message.cancelledAt       = now;
        message.firstIncarnation  = first->soul.incarnation;
        message.secondIncarnation = second->soul.incarnation;
        message.firstEpoch        = first->stateEpoch;
        message.secondEpoch       = second->stateEpoch;
        SendToPair(message, *first, *second);
        KCDC::Scripting::Events::PlayerCombatCancelled(player, opponent);
        return true;
    }

    bool Combat::OpenSwing(std::uint64_t attackerNetworkId, std::uint32_t generation, std::uint32_t windowMs, std::uint8_t strikesPerVictim, bool counter) {
        // A generation sent again, or one past the attacker's rate, opens
        // nothing: its presentation still plays, but it strikes no one.
        return _ledgers[attackerNetworkId].Relay(generation, MafiaNet::GetTime(), windowMs, strikesPerVictim, counter);
    }

    // A combat event plays on the attacker's body, so it goes to exactly the
    // peers that have that body: the audience interest, virtual worlds and
    // budgets already chose. The attacker's own machine played it first.
    std::size_t Combat::Relay(const KCDC::Shared::RPC::CombatAttack &attack, Framework::Networking::Replication::NetworkEntity &actor) {
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        auto *replication = Framework::CoreModules::GetReplication();
        if (net == nullptr || replication == nullptr) {
            return 0;
        }

        KCDC::Shared::RPC::CombatAttack relay = attack;
        relay.attackerNetworkId               = actor.GetNetworkID();
        relay.relayedAt                       = MafiaNet::GetTime();
        if (attack.phase == KCDC::Shared::RPC::CombatAttack::Commit) {
            if (OpenSwing(actor.GetNetworkID(), attack.attackGeneration, attack.windowMs)) {
                _attacks.emplace_back(actor.GetNetworkID(), attack.opponentNetworkId, relay.relayedAt);
            }
        }

        std::size_t recipients = 0;
        replication->ForEachStreamingPeer(&actor, [&](MafiaNet::PeerGuid peer) {
            if (peer != actor.ownerGUID) {
                net->SendRPC(relay, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, Framework::Networking::Channel::Presentation);
                ++recipients;
            }
        });
        return recipients;
    }

    std::size_t Combat::RelayReaction(const KCDC::Shared::RPC::CombatReaction &reaction, Framework::Networking::Replication::NetworkEntity &actor) {
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        auto *replication = Framework::CoreModules::GetReplication();
        if (net == nullptr || replication == nullptr) {
            return 0;
        }

        KCDC::Shared::RPC::CombatReaction relay = reaction;
        relay.actorNetworkId                    = actor.GetNetworkID();
        relay.relayedAt                         = MafiaNet::GetTime();
        if (reaction.kind == KCDC::Shared::RPC::CombatReaction::Attack) {
            if (OpenSwing(actor.GetNetworkID(), reaction.attackGeneration, reaction.windowMs)) {
                _attacks.emplace_back(actor.GetNetworkID(), reaction.opponentNetworkId, relay.relayedAt);
            }
        }
        if (reaction.kind == KCDC::Shared::RPC::CombatReaction::Riposte) {
            if (OpenSwing(actor.GetNetworkID(), reaction.attackGeneration, reaction.windowMs, 1, true)) {
                _attacks.emplace_back(actor.GetNetworkID(), reaction.opponentNetworkId, relay.relayedAt);
            }
        }
        if (reaction.kind == KCDC::Shared::RPC::CombatReaction::SyncPerfectBlock && reaction.attackGeneration != 0) {
            if (OpenSwing(actor.GetNetworkID(), reaction.attackGeneration, reaction.windowMs, KCDC::Shared::RPC::CombatSwingLedger::kSyncStrikesPerVictim)) {
                _attacks.emplace_back(actor.GetNetworkID(), reaction.opponentNetworkId, relay.relayedAt);
            }
        }

        // An NPC's action goes out under its own message: player presentation
        // admission would name the receiving connection's player as its actor.
        const bool npc         = replication->GetEntity<KCDC::Shared::Entities::NpcEntity>(actor.GetNetworkID()) != nullptr;
        std::size_t recipients = 0;
        replication->ForEachStreamingPeer(&actor, [&](MafiaNet::PeerGuid peer) {
            if (peer == actor.ownerGUID) {
                return;
            }
            if (npc) {
                KCDC::Shared::RPC::NpcCombatReaction message {relay};
                net->SendRPC(message, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, Framework::Networking::Channel::Presentation);
            }
            else {
                net->SendRPC(relay, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, Framework::Networking::Channel::Presentation);
            }
            ++recipients;
        });
        return recipients;
    }

    bool Combat::CanFight(const KCDC::Shared::Entities::HumanEntity &attacker, const KCDC::Shared::Entities::HumanEntity &victim) {
        return victim.soul.valid && victim.soul.CanAct() && attacker.soul.valid && attacker.GetVirtualWorld() == victim.GetVirtualWorld();
    }

    bool Combat::WithinReach(const KCDC::Shared::Entities::HumanEntity &attacker, const KCDC::Shared::Entities::HumanEntity &victim) {
        // A body whose owner has not placed it yet stands nowhere a blade
        // could reach.
        return attacker.transformReady && victim.transformReady && glm::distance(attacker.position, victim.position) <= kMaxMeleeReach;
    }

    void Combat::OnHitReport(const KCDC::Shared::RPC::CombatHitReport &report, MafiaNet::Packet *packet) {
        const auto logger = Framework::Logging::GetLogger(kLogger);
        auto *replication = Framework::CoreModules::GetReplication();
        if (packet == nullptr || replication == nullptr) {
            return;
        }
        if (!report.IsSane()) {
            logger->warn("Rejected a malformed hit report from peer {}", static_cast<std::uint64_t>(MafiaNet::ToPeerGuid(packet->guid)));
            return;
        }

        // A blow with an NPC on either side: the victim is the NPC its
        // simulator names, or the sender's own player for an NPC's blow.
        const Combatant attacker = Combatant::Find(report.attackerNetworkId);
        if (report.reporterNetworkId != 0 || attacker.npc != nullptr) {
            if (report.IsClaim() || report.IsEmptyTouch() || !attacker || replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(MafiaNet::ToPeerGuid(packet->guid)) == nullptr) {
                return;
            }
            const Combatant victim = Combatant::Sender(report.reporterNetworkId, MafiaNet::ToPeerGuid(packet->guid));
            if (!victim || (report.reporterNetworkId != 0 && victim.npc == nullptr)) {
                return;
            }
            _reports.Push({report, victim.entity->GetNetworkID(), MafiaNet::GetTime(), attacker.Incarnation(), attacker.entity->stateEpoch});
            return;
        }

        // Whoever sent it is the side it speaks for: a client only ever
        // reports blows on its own body, and only ever claims its own.
        auto *sender = replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(MafiaNet::ToPeerGuid(packet->guid));
        if (sender == nullptr) {
            return;
        }
        // A report from a past life, or a touch that carries no blow, is
        // nothing to rule on.
        if (!sender->soul.valid || report.incarnation != sender->soul.incarnation || report.epoch != sender->stateEpoch || report.IsEmptyTouch()) {
            return;
        }
        auto *other = replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(report.IsClaim() ? report.victimNetworkId : report.attackerNetworkId);
        if (other == nullptr || other == sender || !other->soul.valid) {
            return;
        }
        _reports.Push({report, sender->GetNetworkID(), MafiaNet::GetTime(), other->soul.incarnation, other->stateEpoch});
    }

    bool Combat::ProcessReport(const KCDC::Shared::RPC::CombatReportQueue::Entry &entry) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return true;
        }
        const auto &report = entry.report;
        if (report.reporterNetworkId != 0 || replication->GetEntity<KCDC::Shared::Entities::NpcEntity>(report.attackerNetworkId) != nullptr) {
            return ProcessNpcReport(entry);
        }
        if (WasCancelled(entry.sender, report.IsClaim() ? report.victimNetworkId : report.attackerNetworkId, entry.receivedAt)) {
            return true;
        }
        auto *sender = replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(entry.sender);
        auto *other  = replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(report.IsClaim() ? report.victimNetworkId : report.attackerNetworkId);
        if (sender == nullptr || other == nullptr || !sender->soul.valid || !other->soul.valid || sender->soul.incarnation != report.incarnation || sender->stateEpoch != report.epoch || other->soul.incarnation != entry.otherIncarnation || other->stateEpoch != entry.otherEpoch) {
            return true;
        }
        if (report.IsClaim() && report.attackerNetworkId != entry.sender) {
            return true;
        }
        if (report.Source() == KCDC::Shared::RPC::CombatHitSource::Melee) {
            const auto ledger = _ledgers.find(report.attackerNetworkId);
            const auto check  = ledger != _ledgers.end() ? ledger->second.Check(report.attackGeneration, report.IsClaim() ? other->GetNetworkID() : entry.sender, MafiaNet::GetTime(), kStrikeSlackMs) : KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing;
            if (check == KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing) {
                return false;
            }
        }
        if (report.IsClaim()) {
            OnAttackerClaim(report, *sender);
        }
        else {
            OnVictimReport(report, *sender);
        }
        return true;
    }

    void Combat::OnVictimReport(const KCDC::Shared::RPC::CombatHitReport &report, KCDC::Shared::Entities::HumanEntity &victim) {
        const auto logger                               = Framework::Logging::GetLogger(kLogger);
        auto *attacker                                  = Framework::CoreModules::GetReplication()->GetEntity<KCDC::Shared::Entities::HumanEntity>(report.attackerNetworkId);
        const KCDC::Shared::RPC::CombatHitSource source = report.Source();
        if (attacker == nullptr || attacker == &victim) {
            logger->warn("Rejected a {} hit report naming no other player: victim={} attacker={}", SourceName(source), victim.GetNetworkID(), report.attackerNetworkId);
            return;
        }
        if (!CanFight(*attacker, victim)) {
            return;
        }

        const std::uint64_t now = MafiaNet::GetTime();
        if (source == KCDC::Shared::RPC::CombatHitSource::Melee) {
            // The swing has to be one this server relayed, still able to land,
            // and not one that already struck this body -- an attacker's claim
            // ruled on first is the same blow.
            const auto ledger                                        = _ledgers.find(attacker->GetNetworkID());
            const KCDC::Shared::RPC::CombatSwingLedger::Result check = ledger != _ledgers.end() ? ledger->second.Check(report.attackGeneration, victim.GetNetworkID(), now, kStrikeSlackMs) : KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing;
            if (check == KCDC::Shared::RPC::CombatSwingLedger::Result::AlreadyStruck) {
                return;
            }
            if (check != KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted) {
                logger->warn("Rejected a melee hit: attacker={} victim={} generation={} reason={}", attacker->GetNetworkID(), victim.GetNetworkID(), report.attackGeneration, KCDC::Shared::RPC::CombatSwingLedger::ResultName(check));
                return;
            }
            // Out of reach, the swing is not spent: the attacker's own claim of
            // it may still stand.
            if (!WithinReach(*attacker, victim)) {
                logger->warn("Rejected a melee hit from out of reach: attacker={} victim={} generation={} distance={:.2f}", attacker->GetNetworkID(), victim.GetNetworkID(), report.attackGeneration, glm::distance(attacker->position, victim.position));
                return;
            }
            ledger->second.Strike(report.attackGeneration, victim.GetNetworkID(), now, kStrikeSlackMs);

            // The victim saw it, against the guard it really held: whatever
            // the attacker's machine claimed of the same swing is moot.
            _claims.erase({attacker->GetNetworkID(), report.attackGeneration, victim.GetNetworkID()});
        }
        else if (_missiles == nullptr || !_missiles->ClaimDamage(attacker->GetNetworkID(), report.shotSequence, victim.GetNetworkID(), now)) {
            logger->warn("Rejected a missile hit no accepted impact owes: shooter={} victim={} sequence={}", attacker->GetNetworkID(), victim.GetNetworkID(), report.shotSequence);
            return;
        }

        Rule(report, *attacker, victim, KCDC::Shared::RPC::CombatHitResolver::Victim, now);
    }

    void Combat::OnAttackerClaim(const KCDC::Shared::RPC::CombatHitReport &report, KCDC::Shared::Entities::HumanEntity &attacker) {
        const auto logger = Framework::Logging::GetLogger(kLogger);
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        auto *victim      = Framework::CoreModules::GetReplication()->GetEntity<KCDC::Shared::Entities::HumanEntity>(report.victimNetworkId);
        if (report.attackerNetworkId != attacker.GetNetworkID() || victim == nullptr || victim == &attacker) {
            logger->warn("Rejected a hit claim naming no other player: attacker={} claimedAttacker={} victim={}", attacker.GetNetworkID(), report.attackerNetworkId, report.victimNetworkId);
            return;
        }
        if (!CanFight(attacker, *victim)) {
            return;
        }
        // A claim is priced here or not taken: the health an unpriced one
        // would take is the attacker's own figure.
        if (!report.hasInputs || report.attackTypeId < 0) {
            logger->warn("Rejected a hit claim with no hit data to price: attacker={} victim={} generation={}", attacker.GetNetworkID(), victim->GetNetworkID(), report.attackGeneration);
            return;
        }

        const std::uint64_t now                            = MafiaNet::GetTime();
        const auto ledger                                  = _ledgers.find(attacker.GetNetworkID());
        KCDC::Shared::RPC::CombatSwingLedger::Result check = ledger != _ledgers.end() ? ledger->second.Check(report.attackGeneration, victim->GetNetworkID(), now, kStrikeSlackMs) : KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing;
        // A claim is the fallback for a swing the victim saw nothing of: once
        // the victim reported it, every contact of it is the victim's to tell.
        if (check == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted && ledger->second.StrikesOn(report.attackGeneration, victim->GetNetworkID()) > 0) {
            check = KCDC::Shared::RPC::CombatSwingLedger::Result::AlreadyStruck;
        }
        // Already struck is the common case: the victim's own report of this
        // swing got here first.
        if (check != KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted) {
            return;
        }
        if (!WithinReach(attacker, *victim)) {
            logger->warn("Rejected a hit claim from out of reach: attacker={} victim={} generation={} distance={:.2f}", attacker.GetNetworkID(), victim->GetNetworkID(), report.attackGeneration, glm::distance(attacker.position, victim->position));
            return;
        }

        // Held for as long as the victim's own report of this swing can take
        // to get here: their round trip, and a frame or two.
        const std::uint64_t ping = net != nullptr && net->GetPeer() != nullptr ? static_cast<std::uint64_t>(std::max(net->GetPeer()->GetAveragePing(MafiaNet::ToGuid(victim->ownerGUID)), 0)) : 0;
        const std::uint64_t hold = std::clamp(ping + kClaimHoldMarginMs, kClaimHoldMinimumMs, kClaimHoldMaximumMs);
        HeldClaim held;
        held.report            = report;
        held.ruleAt            = now + hold;
        held.claimedAt         = now;
        held.victimIncarnation = victim->soul.incarnation;
        held.victimEpoch       = victim->stateEpoch;
        _claims.try_emplace({attacker.GetNetworkID(), report.attackGeneration, victim->GetNetworkID()}, held);
    }

    void Combat::Update() {
        // An NPC's swings belong to one life and one order; either ending closes them.
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            std::erase_if(_npcSwingContexts, [this, replication](const auto &entry) {
                const auto *npc = replication->GetEntity<KCDC::Shared::Entities::NpcEntity>(entry.first);
                if (npc != nullptr && npc->spawnEpoch == entry.second.incarnation && npc->intentRevision == entry.second.revision) {
                    return false;
                }
                _ledgers.erase(entry.first);
                return true;
            });
        }
        std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> attacks;
        attacks.swap(_attacks);
        for (const auto &[player, opponent, at] : attacks) {
            if (WasCancelled(player, opponent, at)) {
                continue;
            }
            auto *replication = Framework::CoreModules::GetReplication();
            auto *first       = replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(player);
            auto *second      = replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(opponent);
            if (first != nullptr && second != nullptr && first != second && CanFight(*first, *second) && WithinReach(*first, *second)) {
                KCDC::Scripting::Events::PlayerAttack(player, opponent);
            }
        }
        // Presentation admission runs first in PostUpdate. A replicated state
        // fence can defer it further; keep early reports for that bounded wait.
        _reports.Drain(MafiaNet::GetTime(), [this](const auto &entry) {
            return ProcessReport(entry);
        });
        std::vector<std::pair<KCDC::Shared::RPC::CombatClaimAnswer, std::uint64_t>> answers;
        answers.swap(_answers);
        for (const auto &[answer, sender] : answers) {
            OnClaimAnswer(answer, sender);
        }
        if (_claims.empty()) {
            return;
        }
        auto *net               = Framework::CoreModules::GetNetworkPeer();
        auto *replication       = Framework::CoreModules::GetReplication();
        const std::uint64_t now = MafiaNet::GetTime();

        // Taken out before any is ruled on: a ruling runs the gamemode's
        // handler, which may reach back in here.
        _dueClaims.clear();
        for (auto claim = _claims.begin(); claim != _claims.end();) {
            HeldClaim &held = claim->second;
            if (held.ruleAt > now) {
                ++claim;
                continue;
            }

            // The victim never said: its copy of the swing missed it, or never
            // played. Its machine is asked how it was really defending before
            // the attacker's account is taken.
            auto *victim = replication != nullptr ? replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(held.report.victimNetworkId) : nullptr;
            if (held.queryId == 0 && net != nullptr && victim != nullptr && victim->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
                KCDC::Shared::RPC::CombatClaimQuery query;
                query.queryId           = ++_lastQuery;
                query.attackerNetworkId = held.report.attackerNetworkId;
                query.attackGeneration  = held.report.attackGeneration;
                net->SendRPC(query, MafiaNet::ToGuid(victim->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, Framework::Networking::Channel::Presentation);
                const std::uint64_t ping = net->GetPeer() != nullptr ? static_cast<std::uint64_t>(std::max(net->GetPeer()->GetAveragePing(MafiaNet::ToGuid(victim->ownerGUID)), 0)) : 0;
                held.queryId             = query.queryId;
                held.ruleAt              = now + std::clamp(ping + kQueryMarginMs, kQueryMinimumMs, kQueryMaximumMs);
                ++claim;
                continue;
            }

            // No answer in time: the attacker's engine saw the blade land, and
            // that stands.
            _dueClaims.push_back(held);
            claim = _claims.erase(claim);
        }
        for (const HeldClaim &held : _dueClaims) {
            RuleClaim(held, now);
        }
    }

    void Combat::OnClaimAnswer(const KCDC::Shared::RPC::CombatClaimAnswer &answer, std::uint64_t senderId) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || !answer.IsSane()) {
            return;
        }
        auto *sender = replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(senderId);
        if (sender == nullptr) {
            return;
        }
        const auto held = std::find_if(_claims.begin(), _claims.end(), [&answer](const auto &claim) {
            return claim.second.queryId == answer.queryId;
        });
        // Only the victim speaks for its own guard.
        if (held == _claims.end() || held->second.report.victimNetworkId != sender->GetNetworkID()) {
            return;
        }
        HeldClaim claim = held->second;
        _claims.erase(held);
        answer.Amend(claim.report);
        RuleClaim(claim, MafiaNet::GetTime());
    }

    void Combat::RuleClaim(const HeldClaim &claim, std::uint64_t now) {
        const KCDC::Shared::RPC::CombatHitReport &report = claim.report;
        auto *replication                                = Framework::CoreModules::GetReplication();
        auto *attacker                                   = replication != nullptr ? replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(report.attackerNetworkId) : nullptr;
        auto *victim                                     = replication != nullptr ? replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(report.victimNetworkId) : nullptr;
        if (attacker == nullptr || victim == nullptr || !CanFight(*attacker, *victim)) {
            return;
        }
        // A victim that died and came back while the claim was held is not
        // the body the blade met.
        if (victim->soul.incarnation != claim.victimIncarnation || victim->stateEpoch != claim.victimEpoch || attacker->soul.incarnation != report.incarnation || attacker->stateEpoch != report.epoch) {
            return;
        }
        const auto ledger                                         = _ledgers.find(attacker->GetNetworkID());
        const bool victimSpoke                                    = ledger != _ledgers.end() && ledger->second.StrikesOn(report.attackGeneration, victim->GetNetworkID()) > 0;
        const KCDC::Shared::RPC::CombatSwingLedger::Result strike = victimSpoke                ? KCDC::Shared::RPC::CombatSwingLedger::Result::AlreadyStruck
                                                                    : ledger != _ledgers.end() ? ledger->second.Strike(report.attackGeneration, victim->GetNetworkID(), now, kStrikeSlackMs + static_cast<std::uint32_t>(kClaimHoldMaximumMs + kQueryMaximumMs))
                                                                                               : KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing;
        if (strike != KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted) {
            return;
        }
        Rule(report, *attacker, *victim, KCDC::Shared::RPC::CombatHitResolver::Attacker, claim.claimedAt);
    }

    void Combat::Rule(const KCDC::Shared::RPC::CombatHitReport &report, KCDC::Shared::Entities::HumanEntity &attacker, KCDC::Shared::Entities::HumanEntity &victim, KCDC::Shared::RPC::CombatHitResolver resolver, std::uint64_t reportedAt) {
        const KCDC::Shared::RPC::CombatHitSource source = report.Source();
        const std::uint8_t bodyPart                     = KCDC::Shared::Game::BodyPartOfSubpart(report.hitSubpart);
        const std::uint8_t limb                         = KCDC::Shared::Game::FindHumanLimb(bodyPart) != nullptr ? bodyPart : KCDC::Shared::Game::kNoBodyPart;
        const auto reason                               = KCDC::Shared::Game::ToDamageReason(report.reason);

        // A swing is priced here, from what the server holds of both fighters.
        // The engine's own figure stands for an arrow, and for a victim's
        // report of a swing whose hit data never arrived -- the victim's
        // word about its own body, held to the engine's own clamp.
        const auto ledger                           = _ledgers.find(attacker.GetNetworkID());
        const bool counter                          = source == KCDC::Shared::RPC::CombatHitSource::Melee && ledger != _ledgers.end() && ledger->second.IsCounter(report.attackGeneration);
        const std::optional<CombatBlow> blow        = CombatBlow::From(report, attacker, victim, counter);
        const KCDC::Shared::Game::CombatPrice price = blow ? blow->Price() : KCDC::Shared::Game::CombatPrice {};
        const float unpriced                        = source == KCDC::Shared::RPC::CombatHitSource::Melee ? std::min(report.healthLoss, KCDC::Shared::Game::CombatPricingParams::kMaxDamage) : report.healthLoss;

        // The gamemode's say: refuse the blow, or change what it takes. The
        // presentation of the blow is left alone either way -- the blades met.
        KCDC::Scripting::Events::PlayerHitProposal proposal;
        proposal.damage              = blow ? price.health : unpriced;
        proposal.engineDamage        = report.healthLoss;
        proposal.stamina             = report.staminaLoss;
        proposal.bodyPart            = limb;
        proposal.reason              = reason;
        proposal.missile             = source == KCDC::Shared::RPC::CombatHitSource::Missile;
        proposal.flags               = report.flags;
        proposal.priced              = blow.has_value();
        proposal.attack              = price.attack;
        proposal.armor               = price.armor;
        proposal.block               = price.block;
        proposal.bodyPartCoefficient = blow ? price.subpartCoefficient : 1.0F;
        proposal.weapon              = blow && blow->Weapon() != nullptr ? KCDC::Scripting::Detail::FormatItemClass(blow->Weapon()->itemClass) : std::string();
        const auto beforeHandler     = MafiaNet::GetTime();
        const bool allowed           = KCDC::Scripting::Events::PlayerHit(victim.GetNetworkID(), attacker.GetNetworkID(), proposal);
        if (WasCancelled(attacker.GetNetworkID(), victim.GetNetworkID(), beforeHandler)) {
            return;
        }

        // A handler's scales price the blow again; a damage it set stands.
        float decided = proposal.damage;
        if (blow && !proposal.damageSet && !(proposal.modifiers == KCDC::Shared::Game::CombatPricingScales {})) {
            decided = blow->Price(proposal.modifiers).health;
        }
        const float ruledLoss = allowed && std::isfinite(decided) ? std::clamp(decided, 0.0F, KCDC::Shared::RPC::CombatHitReport::kMaxHealthLoss) : 0.0F;
        KCDC::Shared::RPC::CombatOutcome outcome;
        outcome.sequence          = ++_lastRuling;
        outcome.source            = report.source;
        outcome.resolver          = static_cast<std::uint8_t>(resolver);
        outcome.attackerNetworkId = attacker.GetNetworkID();
        outcome.victimNetworkId   = victim.GetNetworkID();
        outcome.victimIncarnation = victim.soul.incarnation;
        outcome.victimEpoch       = victim.stateEpoch;
        outcome.attackGeneration  = report.attackGeneration;
        outcome.shotSequence      = report.shotSequence;
        outcome.healthLoss        = ruledLoss;
        // A victim's report spent its stamina at contact; after a claim the
        // victim spends what the server priced, against its real guard.
        const bool claimedPrice = blow && resolver == KCDC::Shared::RPC::CombatHitResolver::Attacker;
        outcome.staminaLoss     = allowed ? (claimedPrice ? price.stamina : report.staminaLoss) : 0.0F;
        // A punch or a kick knocks out rather than kills (`cannot_kill`); the
        // engine's own figure says so of a blow the server did not price,
        // unless the gamemode made it heavier than the game did.
        outcome.mayKill        = blow ? price.mayKill : report.engineLethal || ruledLoss > unpriced;
        outcome.reason         = report.reason;
        outcome.flags          = report.flags;
        outcome.recoilRowIndex = report.recoilRowIndex;
        outcome.hitRowIndex    = report.hitRowIndex;
        outcome.hitRecordFlags = report.hitRecordFlags;
        outcome.hasImpact      = report.hasImpact;
        outcome.impact         = report.impact;
        outcome.ruledAt        = MafiaNet::GetTime();
        outcome.reportedAt     = reportedAt;

        Broadcast(outcome, attacker, victim);
        Framework::Logging::GetLogger(kLogger)->debug("Ruled a {} hit #{} on the {}'s word: attacker={} victim={} generation={} shot={} ruled={:.2f} refused={}", SourceName(source), outcome.sequence,
            resolver == KCDC::Shared::RPC::CombatHitResolver::Attacker ? "attacker" : "victim", attacker.GetNetworkID(), victim.GetNetworkID(), report.attackGeneration, report.shotSequence, ruledLoss, !allowed);

        if (ruledLoss > 0.0F) {
            KCDC::Scripting::Events::PlayerDamaged(victim.GetNetworkID(), attacker.GetNetworkID(), ruledLoss, limb, reason);
        }
    }

    void Combat::Broadcast(const KCDC::Shared::RPC::CombatOutcome &outcome, KCDC::Shared::Entities::HumanEntity &attacker, KCDC::Shared::Entities::HumanEntity &victim) {
        SendToPair(outcome, attacker, victim);
    }
} // namespace KCDC::Features::Player
