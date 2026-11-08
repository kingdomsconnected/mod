#include "presentation_module.h"

#include "core/application.h"
#include "core/obfuscation.h"
#include "features/door/door_service.h"
#include "features/player/player.h"
#include "features/player/player_duel_service.h"
#include "common/module_support.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <utility>

namespace KCDC::Core {
    namespace {
        constexpr const char *kLogger                    = "Presentation";
        constexpr std::size_t kMaxIncomingEvents         = 4096;
        constexpr std::size_t kMaxLocalReservations      = 256;
        constexpr std::size_t kMaxRemoteEvents           = 4096;
        constexpr std::size_t kMaxRemoteEventsPerActor   = 128;
        constexpr std::size_t kMaxEventsProcessedPerTick = 256;
        constexpr std::uint64_t kIdentityWaitMaximumMs   = 5000;
        // A native attack the engine refuses is retried on this interval
        // rather than once per frame: the call reaches into the combat actor,
        // and nothing it waits on resolves within a single frame anyway.
        constexpr std::uint64_t kNativeRetryIntervalMs = 100;

        // How long a swing the engine refuses is retried before it is
        // dropped: about one swing's length. Past that the owner has moved on
        // -- often out of combat altogether -- and a swing played now would
        // contradict the body's replicated state rather than show it.
        constexpr std::uint64_t kNativeRetryMaximumMs = 1000;

        // How long an event waits for its actor's drawn pose to reach the
        // moment the server relayed it: twice the deepest the interpolation
        // delay goes (200 ms). Past it the event plays anyway.
        constexpr std::uint64_t kPoseClockWaitMaximumMs = 400;

        /**
         * The server's relay stamp moved onto this machine's clock, as RakNet
         * moves the timestamp of every relayed pose: the same shift, against
         * the same connection, so events and poses share one timeline.
         */
        MafiaNet::Time ToLocalClock(MafiaNet::Time relayedAt, const MafiaNet::Packet *packet) {
            auto *net = Framework::CoreModules::GetNetworkPeer();
            if (relayedAt == 0 || packet == nullptr || net == nullptr || net->GetPeer() == nullptr) {
                return 0;
            }
            return relayedAt - net->GetPeer()->GetClockDifferential(packet->guid);
        }

        // How long a vault is retried against an engine that keeps finding
        // nothing to vault. Much shorter than the identity wait: a retrying
        // event sits at the head of its actor's stream, so this is what a
        // refused vault may hold that player's swings up for. Half
        // a second covers the pose interpolation delay and its jitter, which
        // is the only lag a vault ever waits on.
        constexpr std::uint64_t kLedgeGrabRetryBudgetMs = 500;

        // How long a combat reaction is retried while the bodies it names
        // settle -- a partner the engine has not yet promoted to the replaying
        // body's opponent, which it does at that body's next rest. Short and
        // per frame: a consequence played late is a consequence played wrong,
        // and what it waits on lands within frames or not at all.
        constexpr std::uint64_t kReactionRetryIntervalMs = 16;
        constexpr std::uint64_t kReactionRetryBudgetMs   = 250;

        /** Whether this player's own body is the one a network id names. */
        bool IsLocalBody(std::uint64_t networkId) {
            const KCDC::Features::Player::Human *human = networkId != 0 ? KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(networkId) : nullptr;
            return human != nullptr && human->IsOwner();
        }

        // Which body a presentation is about. Each wire type names it after
        // what it does rather than sharing one field, so the read is spelled
        // out per type instead of being reached through a common member.
        std::uint64_t PresentationActor(const Shared::RPC::DoorInteraction &interaction) {
            return interaction.actorNetworkId;
        }

        std::uint64_t PresentationActor(const Shared::RPC::CombatAttack &attack) {
            return attack.attackerNetworkId;
        }

        std::uint64_t PresentationActor(const Shared::RPC::PlayerLedgeGrab &grab) {
            return grab.climberNetworkId;
        }

        std::uint64_t PresentationActor(const Shared::RPC::CombatReaction &reaction) {
            return reaction.actorNetworkId;
        }
    } // namespace

    PresentationModule &PresentationModule::Get() {
        static PresentationModule instance;
        return instance;
    }

    void PresentationModule::Install() {
        KCDC_JUNK;
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr || !KCDC_OPAQUE_TRUE) {
            Framework::Logging::GetLogger(kLogger)->error("Cannot register presentation RPCs: network peer is unavailable during client initialization");
            return;
        }

        net->RegisterRPC<Shared::RPC::DoorInteraction>([](const Shared::RPC::DoorInteraction &interaction, MafiaNet::Packet *packet) {
            if (!interaction.IsSane()) {
                Framework::Logging::GetLogger(kLogger)->warn("Rejected malformed door interaction: guid={:#x}, sequence={}, actor={}", interaction.doorGuid, interaction.presentationSequence, interaction.actorNetworkId);
                return;
            }
            Shared::RPC::DoorInteraction local = interaction;
            local.relayedAt                    = ToLocalClock(interaction.relayedAt, packet);
            PresentationModule::Get().QueueDoorInteraction(local);
        });
        net->RegisterRPC<Shared::RPC::CombatAttack>([](const Shared::RPC::CombatAttack &attack, MafiaNet::Packet *packet) {
            if (!attack.IsSane()) {
                Framework::Logging::GetLogger(kLogger)->warn("Rejected malformed combat presentation: actor={}, sequence={}, phase={}, generation={}, incarnation={}, epoch={}, combatRevision={}, inputClass={}, zone={}, row={}, candidate=({},{}), context=({},{}), stateMode={:#x}",
                    attack.attackerNetworkId, attack.presentationSequence, attack.PhaseName(), attack.attackGeneration, attack.incarnation, attack.epoch, attack.combatRevision, attack.inputClassId, attack.zoneId, attack.attackRowIndex,
                    static_cast<unsigned>(attack.candidateFlag0), static_cast<unsigned>(attack.candidateFlag1), static_cast<unsigned>(attack.contextFlag0), static_cast<unsigned>(attack.contextFlag1), attack.stateMode);
                return;
            }
            Shared::RPC::CombatAttack local = attack;
            local.relayedAt                 = ToLocalClock(attack.relayedAt, packet);
            PresentationModule::Get().QueueCombatAttack(local);
        });
        net->RegisterRPC<Shared::RPC::PlayerLedgeGrab>([](const Shared::RPC::PlayerLedgeGrab &grab, MafiaNet::Packet *packet) {
            if (!grab.IsSane()) {
                Framework::Logging::GetLogger(kLogger)->warn("Rejected malformed ledge grab presentation: climber={}, sequence={}, incarnation={}, epoch={}, combatRevision={}", grab.climberNetworkId, grab.presentationSequence, grab.incarnation, grab.epoch, grab.combatRevision);
                return;
            }
            Shared::RPC::PlayerLedgeGrab local = grab;
            local.relayedAt                    = ToLocalClock(grab.relayedAt, packet);
            PresentationModule::Get().QueueLedgeGrab(local);
        });
        net->RegisterRPC<Shared::RPC::CombatReaction>([](const Shared::RPC::CombatReaction &reaction, MafiaNet::Packet *packet) {
            if (!reaction.IsSane()) {
                Framework::Logging::GetLogger(kLogger)->warn("Rejected malformed combat reaction: actor={}, sequence={}, kind={}, opponent={}, row={}, pairedRow={}", reaction.actorNetworkId, reaction.presentationSequence, reaction.KindName(), reaction.opponentNetworkId,
                    reaction.rowIndex, reaction.pairedRowIndex);
                return;
            }
            Shared::RPC::CombatReaction local = reaction;
            local.relayedAt                   = ToLocalClock(reaction.relayedAt, packet);
            PresentationModule::Get().QueueCombatReaction(local);
        });
        Framework::Logging::GetLogger(kLogger)->info("Registered common Door/combat/ledge presentation RPC callbacks");
    }

    std::uint64_t PresentationModule::ReserveLocal(bool door, bool terminal) {
        if (!terminal && _localReservations.size() >= kMaxLocalReservations) {
            Framework::Logging::GetLogger(kLogger)->warn("Cannot reserve local {} presentation: {} reservations are still pending", door ? "Door" : "body", kMaxLocalReservations);
            return 0;
        }

        const std::uint64_t sequence = KCDC::Common::NextToken(_nextLocalSequence);
        LocalReservation reservation;
        reservation.door = door;
        _localReservations.emplace(sequence, std::move(reservation));
        return sequence;
    }

    std::uint64_t PresentationModule::ReserveLocalDoor() {
        return ReserveLocal(true);
    }

    bool PresentationModule::CompleteLocalDoor(std::uint64_t presentationSequence, Shared::RPC::DoorInteraction report) {
        const auto reservation = _localReservations.find(presentationSequence);
        if (reservation == _localReservations.end() || !reservation->second.door || !std::holds_alternative<std::monostate>(reservation->second.payload)) {
            Framework::Logging::GetLogger(kLogger)->warn("Rejected completion of unknown or completed local Door reservation {}", presentationSequence);
            return false;
        }

        report.presentationSequence = presentationSequence;
        if (!report.IsSane()) {
            Framework::Logging::GetLogger(kLogger)->warn("Rejected malformed local Door presentation at reservation {}", presentationSequence);
            return false;
        }
        reservation->second.payload = std::move(report);
        return true;
    }

    void PresentationModule::CancelLocalDoor(std::uint64_t presentationSequence) {
        const auto reservation = _localReservations.find(presentationSequence);
        if (reservation == _localReservations.end() || !reservation->second.door) {
            return;
        }
        _localReservations.erase(reservation);
    }

    bool PresentationModule::QueueLocalDoor(Shared::RPC::DoorInteraction report) {
        const std::uint64_t sequence = ReserveLocal(true);
        if (sequence == 0) {
            return false;
        }
        if (!CompleteLocalDoor(sequence, std::move(report))) {
            CancelLocalDoor(sequence);
            return false;
        }
        return true;
    }

    bool PresentationModule::QueueLocalCombatAttack(Shared::RPC::CombatAttack attack) {
        const bool terminal          = attack.phase == Shared::RPC::CombatAttack::Commit || attack.phase == Shared::RPC::CombatAttack::Cancel;
        const std::uint64_t sequence = ReserveLocal(false, terminal);
        if (sequence == 0) {
            return false;
        }

        attack.presentationSequence = sequence;
        if (!attack.IsSane()) {
            _localReservations.erase(sequence);
            Framework::Logging::GetLogger(kLogger)->warn("Rejected malformed local combat presentation: sequence={}, phase={}, generation={}, inputClass={}, zone={}, row={}, candidate=({},{}), context=({},{}), stateMode={:#x}", sequence, attack.PhaseName(),
                attack.attackGeneration, attack.inputClassId, attack.zoneId, attack.attackRowIndex, static_cast<unsigned>(attack.candidateFlag0), static_cast<unsigned>(attack.candidateFlag1), static_cast<unsigned>(attack.contextFlag0), static_cast<unsigned>(attack.contextFlag1),
                attack.stateMode);
            return false;
        }
        _localReservations.at(sequence).payload = std::move(attack);
        return true;
    }

    bool PresentationModule::QueueLocalLedgeGrab(Shared::RPC::PlayerLedgeGrab grab) {
        const std::uint64_t sequence = ReserveLocal(false);
        if (sequence == 0) {
            return false;
        }

        grab.presentationSequence = sequence;
        if (!grab.IsSane()) {
            _localReservations.erase(sequence);
            Framework::Logging::GetLogger(kLogger)->warn("Rejected malformed local ledge grab presentation: sequence={}, incarnation={}, epoch={}, combatRevision={}", sequence, grab.incarnation, grab.epoch, grab.combatRevision);
            return false;
        }
        _localReservations.at(sequence).payload = std::move(grab);
        return true;
    }

    bool PresentationModule::QueueLocalCombatReaction(Shared::RPC::CombatReaction reaction) {
        const std::uint64_t sequence = ReserveLocal(false);
        if (sequence == 0) {
            return false;
        }

        reaction.presentationSequence = sequence;
        if (!reaction.IsSane()) {
            _localReservations.erase(sequence);
            Framework::Logging::GetLogger(kLogger)->warn("Rejected malformed local combat reaction: sequence={}, kind={}, opponent={}, row={}, pairedRow={}", sequence, reaction.KindName(), reaction.opponentNetworkId, reaction.rowIndex, reaction.pairedRowIndex);
            return false;
        }
        _localReservations.at(sequence).payload = std::move(reaction);
        return true;
    }

    void PresentationModule::QueueDoorInteraction(const Shared::RPC::DoorInteraction &interaction) {
        std::lock_guard lock(_incomingMutex);
        if (_acceptIncoming.load() && _incoming.size() < kMaxIncomingEvents) {
            _incoming.emplace_back(interaction);
        }
        else if (_acceptIncoming.load()) {
            ++_droppedIncoming;
        }
    }

    void PresentationModule::QueueCombatAttack(const Shared::RPC::CombatAttack &attack) {
        std::lock_guard lock(_incomingMutex);
        if (_acceptIncoming.load() && _incoming.size() < kMaxIncomingEvents) {
            _incoming.emplace_back(attack);
        }
        else if (_acceptIncoming.load() && attack.phase != Shared::RPC::CombatAttack::Press && _incomingTerminalRecoveries.try_emplace(attack.attackerNetworkId, attack).second) {
            return;
        }
        else if (_acceptIncoming.load()) {
            ++_droppedIncoming;
        }
    }

    void PresentationModule::QueueLedgeGrab(const Shared::RPC::PlayerLedgeGrab &grab) {
        std::lock_guard lock(_incomingMutex);
        if (!_acceptIncoming.load()) {
            return;
        }
        if (_incoming.size() < kMaxIncomingEvents) {
            _incoming.emplace_back(grab);
        }
        else {
            ++_droppedIncoming;
        }
    }

    void PresentationModule::QueueCombatReaction(const Shared::RPC::CombatReaction &reaction) {
        std::lock_guard lock(_incomingMutex);
        if (!_acceptIncoming.load()) {
            return;
        }
        if (_incoming.size() < kMaxIncomingEvents) {
            _incoming.emplace_back(reaction);
        }
        else {
            ++_droppedIncoming;
        }
    }

    void PresentationModule::FlushOutgoing() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        while (!_localReservations.empty()) {
            auto reservation = _localReservations.begin();
            if (std::holds_alternative<std::monostate>(reservation->second.payload)) {
                break;
            }

            if (auto *door = std::get_if<Shared::RPC::DoorInteraction>(&reservation->second.payload)) {
                net->BroadcastRPC(*door, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::UNASSIGNED_RAKNET_GUID, Framework::Networking::Channel::Presentation);
            }
            else if (auto *attack = std::get_if<Shared::RPC::CombatAttack>(&reservation->second.payload)) {
                net->BroadcastRPC(*attack, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::UNASSIGNED_RAKNET_GUID, Framework::Networking::Channel::Presentation);
            }
            else if (auto *grab = std::get_if<Shared::RPC::PlayerLedgeGrab>(&reservation->second.payload)) {
                net->BroadcastRPC(*grab, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::UNASSIGNED_RAKNET_GUID, Framework::Networking::Channel::Presentation);
            }
            else if (auto *reaction = std::get_if<Shared::RPC::CombatReaction>(&reservation->second.payload)) {
                net->BroadcastRPC(*reaction, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, MafiaNet::UNASSIGNED_RAKNET_GUID, Framework::Networking::Channel::Presentation);
            }
            _localReservations.erase(reservation);
        }
    }

    void PresentationModule::DrainIncoming() {
        std::vector<std::variant<Shared::RPC::DoorInteraction, Shared::RPC::CombatAttack, Shared::RPC::PlayerLedgeGrab, Shared::RPC::CombatReaction>> incoming;
        std::unordered_map<std::uint64_t, Shared::RPC::CombatAttack> terminalRecoveries;
        std::size_t droppedIncoming = 0;
        {
            std::lock_guard lock(_incomingMutex);
            incoming.swap(_incoming);
            terminalRecoveries.swap(_incomingTerminalRecoveries);
            droppedIncoming = std::exchange(_droppedIncoming, 0);
        }
        for (auto &recovery : terminalRecoveries) {
            incoming.emplace_back(std::move(recovery.second));
        }

        for (std::variant<Shared::RPC::DoorInteraction, Shared::RPC::CombatAttack, Shared::RPC::PlayerLedgeGrab, Shared::RPC::CombatReaction> &event : incoming) {
            const std::uint64_t actorNetworkId = std::visit([](const auto &payload) { return PresentationActor(payload); }, event);
            const std::uint64_t sequence       = std::visit([](const auto &payload) { return payload.presentationSequence; }, event);
            RemoteActorStream &stream          = _remoteActors[actorNetworkId];
            if (sequence <= stream.lastDispatchedSequence || stream.pending.contains(sequence)) {
                Framework::Logging::GetLogger(kLogger)->debug("Ignored stale or duplicate presentation: actor={}, sequence={}, lastDispatched={}", actorNetworkId, sequence, stream.lastDispatchedSequence);
                continue;
            }
            const bool pendingFull     = _remotePendingCount >= kMaxRemoteEvents || stream.pending.size() >= kMaxRemoteEventsPerActor;
            const auto *attack         = std::get_if<Shared::RPC::CombatAttack>(&event);
            const bool terminal        = attack != nullptr && attack->phase != Shared::RPC::CombatAttack::Press;
            const bool protectTerminal = pendingFull && terminal && !stream.hasProtectedTerminal;
            if (pendingFull && !protectTerminal) {
                ++_rejectedPending;
                continue;
            }
            RemotePresentation presentation;
            presentation.payload           = std::move(event);
            presentation.receivedAt        = MafiaNet::GetTime();
            presentation.dueAt             = std::visit([](const auto &payload) { return payload.relayedAt; }, presentation.payload);
            presentation.protectedTerminal = protectTerminal;
            stream.pending.emplace(sequence, std::move(presentation));
            stream.hasProtectedTerminal = stream.hasProtectedTerminal || protectTerminal;
            ++_remotePendingCount;
        }
        if (droppedIncoming != 0 || !terminalRecoveries.empty() || _rejectedPending != 0) {
            Framework::Logging::GetLogger(kLogger)->warn("Presentation capacity pressure: inboxBound={} dropped={} recoveredTerminals={} pendingRejected={}", kMaxIncomingEvents, droppedIncoming, terminalRecoveries.size(), _rejectedPending);
        }
        _rejectedPending = 0;
    }

    PresentationModule::ActorFenceState PresentationModule::EvaluateActorFence(std::uint64_t actorNetworkId, std::uint32_t incarnation, std::uint8_t epoch, std::uint32_t combatRevision, bool requireResolvedActor) const {
        KCDC::Features::Player::Human *source = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(actorNetworkId);
        if (source == nullptr || !source->soul.valid) {
            return ActorFenceState::Waiting;
        }
        if (source->soul.incarnation != incarnation) {
            return source->soul.incarnation == 0 || Shared::Game::HumanSoulState::IsNewer(incarnation, source->soul.incarnation) ? ActorFenceState::Waiting : ActorFenceState::Obsolete;
        }
        if (source->stateEpoch != epoch) {
            const std::uint8_t distance = static_cast<std::uint8_t>(epoch - source->stateEpoch);
            return distance != 0 && distance < 0x80U ? ActorFenceState::Waiting : ActorFenceState::Obsolete;
        }
        if (source->combatRevision != combatRevision) {
            if (Shared::Entities::HumanEntity::IsCombatRevisionNewer(combatRevision, source->combatRevision)) {
                return ActorFenceState::Waiting;
            }
            // The native edge is often observed before the next replicated
            // combat sample increments its revision, so a newer durable
            // sample proves the fence has passed rather than obsoleting the
            // reliable ordered event. ApplyCombat cancels a held attack input
            // if that newer state already says fighting is over.
            return ActorFenceState::Ready;
        }
        return requireResolvedActor && !source->IsOwner() && source->ResolveActor() == nullptr ? ActorFenceState::Waiting : ActorFenceState::Ready;
    }

    bool PresentationModule::IsDueOnActorClock(std::uint64_t actorNetworkId, const RemotePresentation &presentation, std::uint64_t now) const {
        if (presentation.dueAt == 0 || now - presentation.receivedAt >= kPoseClockWaitMaximumMs) {
            return true;
        }
        const KCDC::Features::Player::Human *source = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(actorNetworkId);

        // Combat that reaches this machine's own body plays on arrival: a
        // swing at it is resolved here against the real guard, and an answer
        // to its own swing lands inside the hold that swing was given, which
        // is sized to the round trip alone (`SwingHoldSeconds`). Only what
        // this player merely watches waits for the bodies to be drawn there.
        // A swing names the body it is aimed at itself; the swinger's lock-on
        // is not there in every fight.
        if (const auto *attack = std::get_if<Shared::RPC::CombatAttack>(&presentation.payload); attack != nullptr && (IsLocalBody(attack->opponentNetworkId) || (source != nullptr && IsLocalBody(source->combatTargetNetworkId)))) {
            return true;
        }
        if (const auto *reaction = std::get_if<Shared::RPC::CombatReaction>(&presentation.payload); reaction != nullptr && IsLocalBody(reaction->opponentNetworkId)) {
            return true;
        }
        const MafiaNet::Time drawn = source != nullptr && !source->IsOwner() ? source->PresentedPoseTime() : 0;
        return drawn == 0 || drawn >= presentation.dueAt;
    }

    void PresentationModule::HurryCombatAttack(std::uint64_t attackerNetworkId, std::uint32_t attackGeneration) {
        const auto actor = _remoteActors.find(attackerNetworkId);
        if (actor == _remoteActors.end()) {
            return;
        }
        // The stream plays in order, so everything up to the swing's last
        // edge is hurried with it.
        std::uint64_t last = 0;
        for (const auto &[sequence, presentation] : actor->second.pending) {
            if (const auto *attack = std::get_if<Shared::RPC::CombatAttack>(&presentation.payload); attack != nullptr && attack->attackGeneration != 0 && attack->attackGeneration <= attackGeneration) {
                last = sequence;
            }
        }
        for (auto &[sequence, presentation] : actor->second.pending) {
            if (sequence > last) {
                break;
            }
            presentation.dueAt = 0;
        }
    }

    void PresentationModule::ProcessIncoming() {
        const std::uint64_t now = MafiaNet::GetTime();
        std::size_t processed   = 0;
        for (auto actor = _remoteActors.begin(); actor != _remoteActors.end() && processed < kMaxEventsProcessedPerTick;) {
            RemoteActorStream &stream = actor->second;
            if (stream.pending.empty()) {
                ++actor;
                continue;
            }

            auto event                   = stream.pending.begin();
            const std::uint64_t sequence = event->first;
            if (now < event->second.nextAttemptAt) {
                ++actor;
                continue;
            }
            // Every wait and retry budget below counts from the moment the
            // event fell due, not from its arrival.
            if (event->second.dueSince == 0) {
                if (!IsDueOnActorClock(actor->first, event->second, now)) {
                    ++actor;
                    continue;
                }
                event->second.dueSince = now;
            }
            if (auto *door = std::get_if<Shared::RPC::DoorInteraction>(&event->second.payload)) {
                const ActorFenceState fence = EvaluateActorFence(door->actorNetworkId, door->actorIncarnation, door->actorStateEpoch, door->actorCombatRevision, door->event == Shared::RPC::DoorEvent::Manipulation);
                if (fence == ActorFenceState::Waiting && now - event->second.dueSince < kIdentityWaitMaximumMs) {
                    ++actor;
                    continue;
                }
                if (fence == ActorFenceState::Ready) {
                    KCDC::Core::gApplication->GetDoorService().ApplyRemoteInteraction(*door);
                }
                else {
                    // The body that did this is gone or has moved on. The door
                    // itself is a replicated entity and settles on its own.
                    Framework::Logging::GetLogger(kLogger)->debug("Skipped door interaction whose actor is obsolete: actor={}, sequence={}, guid={:#x}", actor->first, sequence, door->doorGuid);
                }
            }
            else if (auto *grab = std::get_if<Shared::RPC::PlayerLedgeGrab>(&event->second.payload)) {
                const ActorFenceState fence = EvaluateActorFence(grab->climberNetworkId, grab->incarnation, grab->epoch, grab->combatRevision, true);
                if (fence == ActorFenceState::Waiting && now - event->second.dueSince < kIdentityWaitMaximumMs) {
                    ++actor;
                    continue;
                }
                KCDC::Features::Player::Human *source = fence == ActorFenceState::Ready ? KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(grab->climberNetworkId) : nullptr;
                if (fence != ActorFenceState::Ready) {
                    Framework::Logging::GetLogger(kLogger)->debug("Skipped a ledge grab whose actor is obsolete: actor={}, sequence={}", actor->first, sequence);
                }
                else if (source != nullptr && !source->IsOwner() && !source->PerformRemoteLedgeGrab(grab->jumpRequested, grab->crouchRequested)) {
                    // Retried: the event waits for the body to be drawn where
                    // it vaulted, but the snapshot that puts it flush against
                    // the wall can still be a frame or two out.
                    if (now - event->second.dueSince < kLedgeGrabRetryBudgetMs) {
                        event->second.nextAttemptAt = now + kNativeRetryIntervalMs;
                        ++actor;
                        continue;
                    }
                    Framework::Logging::GetLogger(kLogger)->debug("The engine found nothing to vault for a replicated ledge grab: actor={}, sequence={}, waitedMs={}", actor->first, sequence, now - event->second.dueSince);
                }
            }
            else if (auto *reaction = std::get_if<Shared::RPC::CombatReaction>(&event->second.payload)) {
                const ActorFenceState fence = KCDC::Core::gApplication->GetPlayerDuelService().WasCancelled(reaction->actorNetworkId, reaction->opponentNetworkId, reaction->relayedAt)
                                                  ? ActorFenceState::Obsolete
                                                  : EvaluateActorFence(reaction->actorNetworkId, reaction->incarnation, reaction->epoch, reaction->combatRevision, true);
                if (fence == ActorFenceState::Waiting && now - event->second.dueSince < kIdentityWaitMaximumMs) {
                    ++actor;
                    continue;
                }
                if (fence == ActorFenceState::Ready) {
                    const KCDC::Features::Player::PlayerDuelService::ReactionResult result = KCDC::Core::gApplication->GetPlayerDuelService().ApplyRemoteReaction(*reaction);
                    if (result == KCDC::Features::Player::PlayerDuelService::ReactionResult::Deferred && now - event->second.dueSince < kReactionRetryBudgetMs) {
                        event->second.nextAttemptAt = now + kReactionRetryIntervalMs;
                        ++actor;
                        continue;
                    }
                    if (result == KCDC::Features::Player::PlayerDuelService::ReactionResult::Deferred) {
                        Framework::Logging::GetLogger(kLogger)->warn("Expired combat reaction: actor={}, sequence={}, kind={}, opponent={}, row={}, waitedMs={}, last={}", actor->first, sequence, reaction->KindName(), reaction->opponentNetworkId, reaction->rowIndex,
                            now - event->second.dueSince, KCDC::Core::gApplication->GetPlayerDuelService().LastDeferral());
                    }
                }
                else {
                    Framework::Logging::GetLogger(kLogger)->debug("Skipped a combat reaction whose actor is obsolete: actor={}, sequence={}, kind={}", actor->first, sequence, reaction->KindName());
                }
            }
            else {
                Shared::RPC::CombatAttack &attack = std::get<Shared::RPC::CombatAttack>(event->second.payload);
                ActorFenceState fence             = EvaluateActorFence(attack.attackerNetworkId, attack.incarnation, attack.epoch, attack.combatRevision, true);
                if (KCDC::Core::gApplication->GetPlayerDuelService().WasCancelled(attack.attackerNetworkId, attack.opponentNetworkId, attack.relayedAt)) {
                    fence = ActorFenceState::Obsolete;
                }
                if (fence == ActorFenceState::Waiting && now - event->second.dueSince < kIdentityWaitMaximumMs) {
                    ++actor;
                    continue;
                }
                if (fence == ActorFenceState::Waiting) {
                    fence = ActorFenceState::Obsolete;
                    Framework::Logging::GetLogger(kLogger)->warn("Expired combat presentation identity wait: actor={}, sequence={}, phase={}, generation={}, row={}", actor->first, sequence, attack.PhaseName(), attack.attackGeneration, attack.attackRowIndex);
                }
                KCDC::Features::Player::Human *source = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(attack.attackerNetworkId);
                if (fence == ActorFenceState::Ready && source != nullptr && !source->IsOwner()) {
                    const KCDC::Features::Player::Human::RemoteAttackResult result = source->PerformRemoteAttack(attack);
                    if (result == KCDC::Features::Player::Human::RemoteAttackResult::Deferred && now - event->second.dueSince < kNativeRetryMaximumMs) {
                        event->second.nextAttemptAt = now + kNativeRetryIntervalMs;
                        ++actor;
                        continue;
                    }
                    if (result == KCDC::Features::Player::Human::RemoteAttackResult::Deferred) {
                        source->CancelRemoteAttackGeneration(attack.attackGeneration);
                        Framework::Logging::GetLogger(kLogger)->warn("Expired native combat presentation: actor={}, sequence={}, phase={}, generation={}, row={}, waitedMs={}", actor->first, sequence, attack.PhaseName(), attack.attackGeneration, attack.attackRowIndex,
                            now - event->second.dueSince);
                    }
                }
            }

            stream.lastDispatchedSequence = sequence;
            if (event->second.protectedTerminal) {
                stream.hasProtectedTerminal = false;
            }
            stream.pending.erase(event);
            --_remotePendingCount;
            ++processed;
            ++actor;
        }
    }

    bool PresentationModule::HasPendingDoorInteraction(std::uint64_t doorGuid, std::uint32_t copyWorld) {
        const auto onDoor = [doorGuid, copyWorld](const auto &payload) {
            const auto *door = std::get_if<Shared::RPC::DoorInteraction>(&payload);
            return door != nullptr && door->doorGuid == doorGuid && door->copyWorld == copyWorld;
        };
        for (const auto &actor : _remoteActors) {
            for (const auto &event : actor.second.pending) {
                if (onDoor(event.second.payload)) {
                    return true;
                }
            }
        }
        // The network thread can hand one over after this tick's drain.
        std::lock_guard lock(_incomingMutex);
        return std::any_of(_incoming.begin(), _incoming.end(), onDoor);
    }

    void PresentationModule::ForgetActor(std::uint64_t actorNetworkId) {
        const auto actor = _remoteActors.find(actorNetworkId);
        if (actor == _remoteActors.end()) {
            return;
        }
        _remotePendingCount -= actor->second.pending.size();
        _remoteActors.erase(actor);
    }

    void PresentationModule::Update() {
        _acceptIncoming.store(true);
        DrainIncoming();
        ProcessIncoming();
        FlushOutgoing();
    }

    void PresentationModule::Reset() {
        KCDC_JUNK;
        _acceptIncoming.store(false);
        {
            std::lock_guard lock(_incomingMutex);
            _incoming.clear();
            _incomingTerminalRecoveries.clear();
            _droppedIncoming = 0;
        }
        _localReservations.clear();
        _remoteActors.clear();
        _nextLocalSequence  = 1;
        _remotePendingCount = 0;
        _rejectedPending    = 0;
    }
} // namespace KCDC::Core
