#pragma once

#include "core/feature_registry.h"

#include "shared/features/door/door_rpc.h"
#include "shared/features/player/player_combat_attack.h"
#include "shared/features/player/player_combat_reaction.h"
#include "shared/features/player/player_ledge.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>
#include <variant>
#include <vector>

namespace KCDC::Core {
    /**
     * Orders transient whole-body presentation independently of durable state.
     *
     * Door state remains DoorService's responsibility. This module owns the one
     * causal stream shared by body presentations, so an attack cannot pass a
     * door interaction which was accepted first on either peer.
     */
    class PresentationModule final: public KCDC::Core::IFeatureService {
      public:
        static PresentationModule &Get();

        /** Registers the common presentation RPC callbacks once at startup. */
        static void Install();

        /** Drains received events and publishes completed local reservations. */
        void Update() override;

        /** Clears all reservations, received events and active body leases. */
        void Reset() override;

        /** Reserves the local stream position of an accepted door edge. */
        std::uint64_t ReserveLocalDoor();

        /** Makes a reserved door report ready for strictly ordered sending. */
        bool CompleteLocalDoor(std::uint64_t presentationSequence, Shared::RPC::DoorInteraction report);

        /** Retires a door reservation which did not become a valid report. */
        void CancelLocalDoor(std::uint64_t presentationSequence);

        /** Reserves and queues an immediate lock/unlock/lockpick report. */
        bool QueueLocalDoor(Shared::RPC::DoorInteraction report);

        /** Reserves and queues one accepted local attack edge. */
        bool QueueLocalCombatAttack(Shared::RPC::CombatAttack attack);

        /** Reserves and queues the local player's accepted vault. */
        bool QueueLocalLedgeGrab(Shared::RPC::PlayerLedgeGrab grab);

        /** Queues a combat action this machine resolved, in order with its swings. */
        bool QueueLocalCombatReaction(Shared::RPC::CombatReaction reaction);

        /** Network-thread entry for a door interaction someone nearby did. */
        void QueueDoorInteraction(const Shared::RPC::DoorInteraction &interaction);

        /** Network-thread entry for live combat presentation. */
        void QueueCombatAttack(const Shared::RPC::CombatAttack &attack);

        /** Network-thread entry for a vault someone nearby started. */
        void QueueLedgeGrab(const Shared::RPC::PlayerLedgeGrab &grab);

        /** Network thread: one other machine's resolved combat action. */
        void QueueCombatReaction(const Shared::RPC::CombatReaction &reaction);

        /**
         * Whether an interaction someone did on this copy of a door has
         * reached this machine and not been played yet: the door's own state
         * is held for it, so the swing is not overtaken by its outcome.
         * Another world's interaction on the same door holds nothing.
         */
        bool HasPendingDoorInteraction(std::uint64_t doorGuid, std::uint32_t copyWorld);

        /**
         * A blow from `attackerNetworkId`'s swing `attackGeneration` was ruled
         * before this machine played the swing: every pending edge of that
         * actor up to the swing's last one stops waiting for the actor's drawn
         * pose, so the swing plays as soon as its body allows.
         */
        void HurryCombatAttack(std::uint64_t attackerNetworkId, std::uint32_t attackGeneration);

        /** Drops one destroyed body generation and its presentation lease. */
        void ForgetActor(std::uint64_t actorNetworkId);

      private:
        enum class ActorFenceState : std::uint8_t {
            Ready,
            Waiting,
            Obsolete
        };

        struct LocalReservation {
            std::variant<std::monostate, Shared::RPC::DoorInteraction, Shared::RPC::CombatAttack, Shared::RPC::PlayerLedgeGrab, Shared::RPC::CombatReaction> payload;
            bool door = false;
        };

        struct RemotePresentation {
            std::variant<Shared::RPC::DoorInteraction, Shared::RPC::CombatAttack, Shared::RPC::PlayerLedgeGrab, Shared::RPC::CombatReaction> payload;
            std::uint64_t receivedAt    = 0;
            std::uint64_t nextAttemptAt = 0;
            bool protectedTerminal      = false;

            // The server's relay moment on this machine's clock, zero when
            // unstamped; and when the actor's drawn pose reached it, which is
            // what every wait and retry budget after that counts from.
            std::uint64_t dueAt    = 0;
            std::uint64_t dueSince = 0;
        };

        struct RemoteActorStream {
            std::map<std::uint64_t, RemotePresentation> pending;
            std::uint64_t lastDispatchedSequence = 0;
            bool hasProtectedTerminal            = false;
        };

        std::uint64_t ReserveLocal(bool door, bool terminal = false);
        void FlushOutgoing();
        void DrainIncoming();
        void ProcessIncoming();

        /**
         * Whether the body a presentation names is still the body that did it.
         *
         * The soul incarnation and the server-authored epoch must be the ones
         * the sender stamped, and the combat state it was thrown from must
         * have arrived. `requireResolvedActor` also waits for a native body,
         * which only the presentations that animate one need.
         */
        ActorFenceState EvaluateActorFence(std::uint64_t actorNetworkId, std::uint32_t incarnation, std::uint8_t epoch, std::uint32_t combatRevision, bool requireResolvedActor) const;

        /**
         * Whether the body a presentation is about has been drawn up to the
         * moment the server relayed it: the actor's pose is played behind the
         * wire, and an event played on arrival would run ahead of the body.
         * Due at once when unstamped, when the actor draws no pose here, or
         * once `kPoseClockWaitMaximumMs` has passed.
         */
        bool IsDueOnActorClock(std::uint64_t actorNetworkId, const RemotePresentation &presentation, std::uint64_t now) const;

        std::mutex _incomingMutex;
        std::vector<std::variant<Shared::RPC::DoorInteraction, Shared::RPC::CombatAttack, Shared::RPC::PlayerLedgeGrab, Shared::RPC::CombatReaction>> _incoming;
        std::unordered_map<std::uint64_t, Shared::RPC::CombatAttack> _incomingTerminalRecoveries;
        std::atomic<bool> _acceptIncoming {false};

        std::map<std::uint64_t, LocalReservation> _localReservations;
        std::unordered_map<std::uint64_t, RemoteActorStream> _remoteActors;
        std::uint64_t _nextLocalSequence = 1;
        std::size_t _remotePendingCount  = 0;
        std::size_t _droppedIncoming     = 0;
        std::size_t _rejectedPending     = 0;
    };
} // namespace KCDC::Core
