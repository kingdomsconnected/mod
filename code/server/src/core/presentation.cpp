#include "presentation.h"
#include "shared/features/npc/npc_entity.h"

#include "features/player/player_combat_module.h"
#include "features/player/player_ledge_module.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <utility>

namespace KCDC::Core {
    namespace {
        constexpr const char *kLogger                           = "Presentation";
        constexpr std::size_t kMaxIncomingPresentations         = 512;
        constexpr std::size_t kMaxPendingPresentations          = 512;
        constexpr std::size_t kMaxPendingPresentationsPerSender = 32;
        constexpr std::uint64_t kMaxDeferralMs                  = 5000;

        const char *PresentationName(const std::variant<KCDC::Shared::RPC::DoorInteraction, KCDC::Shared::RPC::CombatAttack, KCDC::Shared::RPC::PlayerLedgeGrab, KCDC::Shared::RPC::CombatReaction> &payload) {
            if (std::holds_alternative<KCDC::Shared::RPC::DoorInteraction>(payload)) {
                return "door";
            }
            if (std::holds_alternative<KCDC::Shared::RPC::CombatAttack>(payload)) {
                return "combat-attack";
            }
            if (std::holds_alternative<KCDC::Shared::RPC::CombatReaction>(payload)) {
                return "combat-reaction";
            }
            return "ledge-grab";
        }

        const char *AttackLifecycleResultName(KCDC::Shared::RPC::CombatAttackLifecycle::Result result) {
            switch (result) {
            case KCDC::Shared::RPC::CombatAttackLifecycle::Result::Accepted: return "accepted";
            case KCDC::Shared::RPC::CombatAttackLifecycle::Result::ActiveGenerationExists: return "another attack generation is active";
            case KCDC::Shared::RPC::CombatAttackLifecycle::Result::TerminalWithoutPress: return "terminal edge has no active Press";
            case KCDC::Shared::RPC::CombatAttackLifecycle::Result::TerminalGenerationMismatch: return "terminal edge does not match the active generation";
            }
            return "unknown";
        }
    } // namespace

    void Presentation::Install(KCDC::Features::Door::DoorService &door, KCDC::Features::Player::Combat &combat) {
        _door     = &door;
        _combat   = &combat;
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Presentation RPC installation has no network peer");
            return;
        }

        net->RegisterRPC<KCDC::Shared::RPC::DoorInteraction>([this](const KCDC::Shared::RPC::DoorInteraction &report, MafiaNet::Packet *packet) {
            QueueDoor(report, packet);
        });
        net->RegisterRPC<KCDC::Shared::RPC::CombatAttack>([this](const KCDC::Shared::RPC::CombatAttack &attack, MafiaNet::Packet *packet) {
            QueueAttack(attack, packet);
        });
        net->RegisterRPC<KCDC::Shared::RPC::PlayerLedgeGrab>([this](const KCDC::Shared::RPC::PlayerLedgeGrab &grab, MafiaNet::Packet *packet) {
            QueueLedgeGrab(grab, packet);
        });
        net->RegisterRPC<KCDC::Shared::RPC::CombatReaction>([this](const KCDC::Shared::RPC::CombatReaction &reaction, MafiaNet::Packet *packet) {
            QueueReaction(reaction, packet);
        });
    }

    void Presentation::QueueDoor(const KCDC::Shared::RPC::DoorInteraction &report, MafiaNet::Packet *packet) {
        QueuedPresentation presentation;
        presentation.payload             = report;
        presentation.sequence            = report.presentationSequence;
        presentation.actorIncarnation    = report.actorIncarnation;
        presentation.actorStateEpoch     = report.actorStateEpoch;
        presentation.actorCombatRevision = report.actorCombatRevision;
        Queue(std::move(presentation), packet);
    }

    void Presentation::QueueAttack(const KCDC::Shared::RPC::CombatAttack &attack, MafiaNet::Packet *packet) {
        QueuedPresentation presentation;
        presentation.payload             = attack;
        presentation.sequence            = attack.presentationSequence;
        presentation.actorIncarnation    = attack.incarnation;
        presentation.actorStateEpoch     = attack.epoch;
        presentation.actorCombatRevision = attack.combatRevision;
        Queue(std::move(presentation), packet);
    }

    void Presentation::QueueLedgeGrab(const KCDC::Shared::RPC::PlayerLedgeGrab &grab, MafiaNet::Packet *packet) {
        QueuedPresentation presentation;
        presentation.payload             = grab;
        presentation.sequence            = grab.presentationSequence;
        presentation.actorIncarnation    = grab.incarnation;
        presentation.actorStateEpoch     = grab.epoch;
        presentation.actorCombatRevision = grab.combatRevision;
        Queue(std::move(presentation), packet);
    }

    void Presentation::QueueReaction(const KCDC::Shared::RPC::CombatReaction &reaction, MafiaNet::Packet *packet) {
        QueuedPresentation presentation;
        presentation.payload             = reaction;
        presentation.sequence            = reaction.presentationSequence;
        presentation.actorIncarnation    = reaction.incarnation;
        presentation.actorStateEpoch     = reaction.epoch;
        presentation.actorCombatRevision = reaction.combatRevision;
        Queue(std::move(presentation), packet);
    }

    void Presentation::Queue(QueuedPresentation &&presentation, MafiaNet::Packet *packet) {
        if (packet == nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("Rejected {} presentation without a packet", PresentationName(presentation.payload));
            return;
        }

        presentation.sender     = MafiaNet::ToPeerGuid(packet->guid);
        presentation.receivedAt = MafiaNet::GetTime();
        std::scoped_lock lock(_incomingMutex);
        if (_incoming.size() >= kMaxIncomingPresentations) {
            const auto *attack = std::get_if<KCDC::Shared::RPC::CombatAttack>(&presentation.payload);
            if (attack != nullptr && attack->phase != KCDC::Shared::RPC::CombatAttack::Press && attack->attackerNetworkId == 0 && attack->IsSane()) {
                if (_incomingTerminalRecoveries.try_emplace(presentation.sender, std::move(presentation)).second) {
                    return;
                }
            }
            ++_droppedIncoming;
            return;
        }
        _incoming.push_back(std::move(presentation));
    }

    void Presentation::Update() {
        std::deque<QueuedPresentation> incoming;
        std::unordered_map<MafiaNet::PeerGuid, QueuedPresentation> terminalRecoveries;
        std::size_t dropped = 0;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_incoming);
            terminalRecoveries.swap(_incomingTerminalRecoveries);
            dropped = std::exchange(_droppedIncoming, 0);
        }

        _rejectedPending           = 0;
        _protectedPendingTerminals = 0;
        for (QueuedPresentation &presentation : incoming) {
            Ingest(std::move(presentation));
        }
        for (auto &recovery : terminalRecoveries) {
            Ingest(std::move(recovery.second));
        }

        const std::uint64_t now = MafiaNet::GetTime();
        for (auto &[actorNetworkId, sender] : _senders) {
            while (!sender.pending.empty()) {
                auto presentation                         = sender.pending.begin();
                const PresentationDisposition disposition = Process(actorNetworkId, sender, presentation->first, presentation->second, now);
                if (disposition == PresentationDisposition::Deferred) {
                    break;
                }
                if (presentation->second.protectedTerminal) {
                    sender.hasProtectedTerminal = false;
                }
                sender.pending.erase(presentation);
                --_pendingCount;
            }
        }
        if (dropped != 0 || !terminalRecoveries.empty() || _rejectedPending != 0 || _protectedPendingTerminals != 0) {
            Framework::Logging::GetLogger(kLogger)->warn("Presentation capacity pressure: inboxBound={} dropped={} recoveredTerminals={} pendingRejected={} pendingTerminalsProtected={}", kMaxIncomingPresentations, dropped, terminalRecoveries.size(), _rejectedPending,
                _protectedPendingTerminals);
        }
    }

    void Presentation::Ingest(QueuedPresentation &&presentation) {
        const auto logger = Framework::Logging::GetLogger(kLogger);
        const bool sane   = std::visit(
            [](const auto &payload) {
                return payload.IsSane();
            },
            presentation.payload);
        const auto *attack = std::get_if<KCDC::Shared::RPC::CombatAttack>(&presentation.payload);
        if (!sane) {
            if (attack != nullptr) {
                logger->warn("Rejected malformed combat-attack presentation from peer {}: presentationSequence={} phase={} generation={} incarnation={} epoch={} combatRevision={} inputClass={} zone={} row={} candidate=({},{}) context=({},{}) stateMode={:#x}",
                    static_cast<std::uint64_t>(presentation.sender), presentation.sequence, attack->PhaseName(), attack->attackGeneration, presentation.actorIncarnation, presentation.actorStateEpoch, presentation.actorCombatRevision, attack->inputClassId, attack->zoneId,
                    attack->attackRowIndex, static_cast<unsigned>(attack->candidateFlag0), static_cast<unsigned>(attack->candidateFlag1), static_cast<unsigned>(attack->contextFlag0), static_cast<unsigned>(attack->contextFlag1), attack->stateMode);
            }
            else {
                logger->warn("Rejected malformed {} presentation from peer {}: presentationSequence={} incarnation={} epoch={} combatRevision={}", PresentationName(presentation.payload), static_cast<std::uint64_t>(presentation.sender), presentation.sequence,
                    presentation.actorIncarnation, presentation.actorStateEpoch, presentation.actorCombatRevision);
            }
            return;
        }
        if (attack != nullptr && attack->attackerNetworkId != 0) {
            logger->warn("Rejected combat attack with a client-authored attacker: peer={} attacker={} presentationSequence={}", static_cast<std::uint64_t>(presentation.sender), attack->attackerNetworkId, presentation.sequence);
            return;
        }
        const auto *reaction = std::get_if<KCDC::Shared::RPC::CombatReaction>(&presentation.payload);
        if (reaction != nullptr && reaction->actorNetworkId != 0) {
            logger->warn("Rejected combat reaction with a client-authored actor: peer={} actor={} presentationSequence={}", static_cast<std::uint64_t>(presentation.sender), reaction->actorNetworkId, presentation.sequence);
            return;
        }
        const auto *ledgeGrab = std::get_if<KCDC::Shared::RPC::PlayerLedgeGrab>(&presentation.payload);
        if (ledgeGrab != nullptr && ledgeGrab->climberNetworkId != 0) {
            logger->warn("Rejected ledge grab with a client-authored climber: peer={} climber={} presentationSequence={}", static_cast<std::uint64_t>(presentation.sender), ledgeGrab->climberNetworkId, presentation.sequence);
            return;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        auto *actor       = replication != nullptr ? replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(presentation.sender) : nullptr;
        if (actor == nullptr) {
            logger->warn("Rejected {} presentation from peer {} because it has no Human viewer", PresentationName(presentation.payload), static_cast<std::uint64_t>(presentation.sender));
            return;
        }
        if (presentation.actorStateEpoch != actor->stateEpoch) {
            logger->warn("Rejected {} presentation with a mismatched server-authored epoch: actor={} presentationSequence={} reportEpoch={} actorEpoch={}", PresentationName(presentation.payload), actor->GetNetworkID(), presentation.sequence, presentation.actorStateEpoch,
                actor->stateEpoch);
            return;
        }
        if (presentation.actorIncarnation != actor->soul.incarnation && !KCDC::Shared::Game::HumanSoulState::IsNewer(presentation.actorIncarnation, actor->soul.incarnation)) {
            logger->warn("Rejected {} presentation with a stale actor incarnation: actor={} presentationSequence={} reportIncarnation={} actorIncarnation={}", PresentationName(presentation.payload), actor->GetNetworkID(), presentation.sequence, presentation.actorIncarnation,
                actor->soul.incarnation);
            return;
        }

        const std::uint64_t actorNetworkId = actor->GetNetworkID();
        auto [sender, inserted]            = _senders.try_emplace(actorNetworkId);
        if (inserted) {
            sender->second.sender = presentation.sender;
        }
        else if (sender->second.sender != presentation.sender) {
            logger->warn("Rejected {} presentation for actor {} because peer {} does not match queued peer {}", PresentationName(presentation.payload), actorNetworkId, static_cast<std::uint64_t>(presentation.sender), static_cast<std::uint64_t>(sender->second.sender));
            return;
        }
        // The client holds every later reservation behind an unresolved lower
        // one, then sends every type on the one ReliableOrdered presentation channel.
        // Gaps are cancellations; a non-increasing arrival is never valid.
        if (presentation.sequence <= sender->second.lastReceivedSequence) {
            return;
        }
        const bool pendingFull     = _pendingCount >= kMaxPendingPresentations || sender->second.pending.size() >= kMaxPendingPresentationsPerSender;
        const bool terminal        = attack != nullptr && attack->phase != KCDC::Shared::RPC::CombatAttack::Press;
        const bool protectTerminal = pendingFull && terminal && !sender->second.hasProtectedTerminal;
        if (pendingFull && !protectTerminal) {
            ++_rejectedPending;
            return;
        }

        PendingPresentation pending;
        pending.payload             = std::move(presentation.payload);
        pending.receivedAt          = presentation.receivedAt;
        pending.actorIncarnation    = presentation.actorIncarnation;
        pending.actorStateEpoch     = presentation.actorStateEpoch;
        pending.actorCombatRevision = presentation.actorCombatRevision;
        pending.protectedTerminal   = protectTerminal;
        sender->second.pending.emplace(presentation.sequence, std::move(pending));
        sender->second.lastReceivedSequence = presentation.sequence;
        sender->second.hasProtectedTerminal = sender->second.hasProtectedTerminal || protectTerminal;
        ++_pendingCount;
        _protectedPendingTerminals += protectTerminal ? 1 : 0;
    }

    Presentation::PresentationDisposition Presentation::Process(std::uint64_t actorNetworkId, SenderPresentations &sender, std::uint64_t sequence, PendingPresentation &presentation, std::uint64_t now) {
        const auto logger = Framework::Logging::GetLogger(kLogger);
        auto *replication = Framework::CoreModules::GetReplication();
        auto *actor       = replication != nullptr ? replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(actorNetworkId) : nullptr;
        auto *viewer      = replication != nullptr ? replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(sender.sender) : nullptr;
        if (actor == nullptr || actor != viewer) {
            logger->warn("Rejected queued {} presentation because its sender Human disappeared: actor={} peer={} presentationSequence={}", PresentationName(presentation.payload), actorNetworkId, static_cast<std::uint64_t>(sender.sender), sequence);
            return PresentationDisposition::Consumed;
        }
        if (presentation.actorStateEpoch != actor->stateEpoch) {
            logger->warn("Rejected queued {} presentation after the server-authored epoch changed: actor={} presentationSequence={} reportEpoch={} actorEpoch={}", PresentationName(presentation.payload), actorNetworkId, sequence, presentation.actorStateEpoch, actor->stateEpoch);
            return PresentationDisposition::Consumed;
        }

        bool deferred              = false;
        const char *deferredReason = nullptr;
        if (presentation.actorIncarnation != actor->soul.incarnation) {
            if (KCDC::Shared::Game::HumanSoulState::IsNewer(presentation.actorIncarnation, actor->soul.incarnation)) {
                deferred       = true;
                deferredReason = "actor incarnation";
            }
            else {
                logger->warn("Rejected queued {} presentation after a newer actor incarnation arrived: actor={} presentationSequence={} reportIncarnation={} actorIncarnation={}", PresentationName(presentation.payload), actorNetworkId, sequence, presentation.actorIncarnation,
                    actor->soul.incarnation);
                return PresentationDisposition::Consumed;
            }
        }
        else if (!actor->soul.valid) {
            deferred       = true;
            deferredReason = "valid actor soul";
        }

        const bool exactCombatRevision = presentation.actorCombatRevision == actor->combatRevision;
        if (!deferred && !exactCombatRevision && KCDC::Shared::Entities::HumanEntity::IsCombatRevisionNewer(presentation.actorCombatRevision, actor->combatRevision)) {
            deferred       = true;
            deferredReason = "combat revision";
        }
        if (deferred) {
            if (now >= presentation.receivedAt && now - presentation.receivedAt >= kMaxDeferralMs) {
                logger->warn("Expired deferred {} presentation: actor={} presentationSequence={} reason={} waitedMs={} reportRevision={} actorRevision={} actorUpdateAgeMs={}", PresentationName(presentation.payload), actorNetworkId, sequence, deferredReason, now - presentation.receivedAt, presentation.actorCombatRevision, actor->combatRevision, actor->GetUpdateAge());
                return PresentationDisposition::Consumed;
            }
            return PresentationDisposition::Deferred;
        }

        if (const auto *door = std::get_if<KCDC::Shared::RPC::DoorInteraction>(&presentation.payload)) {
            _door->ProcessInteraction(*door, *actor);
        }
        else if (const auto *grab = std::get_if<KCDC::Shared::RPC::PlayerLedgeGrab>(&presentation.payload)) {
            // One edge, no lifecycle to accept and no durable state to fold:
            // the identity checks above are the whole of what the server has
            // to say about it.
            KCDC::Features::Player::Ledge::Relay(*grab, *actor);
        }
        else if (const auto *reaction = std::get_if<KCDC::Shared::RPC::CombatReaction>(&presentation.payload)) {
            if (_combat->WasCancelled(actorNetworkId, reaction->opponentNetworkId, presentation.receivedAt)) {
                return PresentationDisposition::Consumed;
            }
            // The rows are the sender's engine's business and cannot be
            // checked here, but the player it names can: the other side of
            // the exchange has to be someone else who is in the session.
            const auto namesOtherPlayer = [&](std::uint64_t networkId) {
                auto *other = networkId != 0 ? replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(networkId) : nullptr;
                if (other != nullptr) {
                    return other != actor && other->GetVirtualWorld() == actor->GetVirtualWorld();
                }
                auto *npc = replication->GetEntity<KCDC::Shared::Entities::NpcEntity>(networkId);
                return npc != nullptr && npc->alive && npc->GetVirtualWorld() == actor->GetVirtualWorld();
            };
            if (!reaction->NamesOnlyOtherPlayers(namesOtherPlayer)) {
                logger->warn("Rejected combat reaction naming no other player in the session: actor={} presentationSequence={} kind={} opponent={}", actorNetworkId, sequence, reaction->KindName(), reaction->opponentNetworkId);
                return PresentationDisposition::Consumed;
            }
            _combat->RelayReaction(*reaction, *actor);
        }
        else {
            const KCDC::Shared::RPC::CombatAttack &attack = std::get<KCDC::Shared::RPC::CombatAttack>(presentation.payload);
            if (sender.attackLifecycleIncarnation != presentation.actorIncarnation || sender.attackLifecycleEpoch != presentation.actorStateEpoch) {
                sender.attackLifecycle.Reset();
                sender.attackLifecycleIncarnation = presentation.actorIncarnation;
                sender.attackLifecycleEpoch       = presentation.actorStateEpoch;
            }
            const KCDC::Shared::RPC::CombatAttackLifecycle::Result lifecycleResult = sender.attackLifecycle.Accept(attack);
            if (lifecycleResult != KCDC::Shared::RPC::CombatAttackLifecycle::Result::Accepted) {
                logger->warn("Rejected combat attack lifecycle edge: actor={} presentationSequence={} phase={} attackGeneration={} activeGeneration={} reason={}", actorNetworkId, sequence, attack.PhaseName(), attack.attackGeneration, sender.attackLifecycle.activeGeneration,
                    AttackLifecycleResultName(lifecycleResult));
                return PresentationDisposition::Consumed;
            }
            if (attack.phase == KCDC::Shared::RPC::CombatAttack::Press) {
                sender.attackReceivedAt = presentation.receivedAt;
                sender.attackOpponent   = attack.opponentNetworkId;
            }
            // Consume the terminal even after cancellation so the next Press
            // can open a fresh generation. A held pre-cancel Press cannot commit.
            if (!_combat->WasCancelled(actorNetworkId, sender.attackOpponent, sender.attackReceivedAt) && !_combat->WasCancelled(actorNetworkId, attack.opponentNetworkId, presentation.receivedAt)) {
                _combat->Relay(attack, *actor);
            }
        }
        return PresentationDisposition::Consumed;
    }

    void Presentation::OnPlayerDisconnect(MafiaNet::PeerGuid guid, std::uint64_t actorNetworkId) {
        {
            std::scoped_lock lock(_incomingMutex);
            std::erase_if(_incoming, [guid](const QueuedPresentation &presentation) {
                return presentation.sender == guid;
            });
            _incomingTerminalRecoveries.erase(guid);
        }
        const auto sender          = _senders.find(actorNetworkId);
        std::size_t pendingRemoved = 0;
        if (sender != _senders.end()) {
            pendingRemoved = sender->second.pending.size();
            _pendingCount -= pendingRemoved;
            _senders.erase(sender);
        }
    }
} // namespace KCDC::Core
