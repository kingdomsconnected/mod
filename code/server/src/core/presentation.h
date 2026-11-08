#pragma once

#include "features/door/door_service.h"
#include "features/player/player_combat_module.h"
#include "shared/features/door/door_rpc.h"
#include "shared/features/player/player_combat_attack.h"
#include "shared/features/player/player_combat_reaction.h"
#include "shared/features/player/player_ledge.h"

#include <mafianet/types.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <unordered_map>
#include <variant>

namespace KCDC::Core {
    /** Orders every transient Human presentation event on one causal stream. */
    class Presentation final {
      public:
        void Install(KCDC::Features::Door::DoorService &door, KCDC::Features::Player::Combat &combat);
        void Update();
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid, std::uint64_t actorNetworkId);

      private:
        struct QueuedPresentation {
            std::variant<KCDC::Shared::RPC::DoorInteraction, KCDC::Shared::RPC::CombatAttack, KCDC::Shared::RPC::PlayerLedgeGrab, KCDC::Shared::RPC::CombatReaction> payload;
            MafiaNet::PeerGuid sender         = MafiaNet::UNASSIGNED_PEER_GUID;
            std::uint64_t receivedAt          = 0;
            std::uint64_t sequence            = 0;
            std::uint32_t actorIncarnation    = 0;
            std::uint8_t actorStateEpoch      = 0;
            std::uint32_t actorCombatRevision = 0;
        };

        struct PendingPresentation {
            std::variant<KCDC::Shared::RPC::DoorInteraction, KCDC::Shared::RPC::CombatAttack, KCDC::Shared::RPC::PlayerLedgeGrab, KCDC::Shared::RPC::CombatReaction> payload;
            std::uint64_t receivedAt          = 0;
            std::uint32_t actorIncarnation    = 0;
            std::uint8_t actorStateEpoch      = 0;
            std::uint32_t actorCombatRevision = 0;
            bool protectedTerminal            = false;
        };

        struct SenderPresentations {
            MafiaNet::PeerGuid sender                = MafiaNet::UNASSIGNED_PEER_GUID;
            std::uint64_t lastReceivedSequence       = 0;
            std::uint32_t attackLifecycleIncarnation = 0;
            std::uint8_t attackLifecycleEpoch        = 0;
            bool hasProtectedTerminal                = false;
            KCDC::Shared::RPC::CombatAttackLifecycle attackLifecycle;
            std::uint64_t attackReceivedAt = 0;
            std::uint64_t attackOpponent   = 0;
            std::map<std::uint64_t, PendingPresentation> pending;
        };

        enum class PresentationDisposition {
            Consumed,
            Deferred,
        };

        void QueueDoor(const KCDC::Shared::RPC::DoorInteraction &report, MafiaNet::Packet *packet);
        void QueueAttack(const KCDC::Shared::RPC::CombatAttack &attack, MafiaNet::Packet *packet);
        void QueueLedgeGrab(const KCDC::Shared::RPC::PlayerLedgeGrab &grab, MafiaNet::Packet *packet);
        void QueueReaction(const KCDC::Shared::RPC::CombatReaction &reaction, MafiaNet::Packet *packet);
        void Queue(QueuedPresentation &&presentation, MafiaNet::Packet *packet);
        void Ingest(QueuedPresentation &&presentation);
        PresentationDisposition Process(std::uint64_t actorNetworkId, SenderPresentations &sender, std::uint64_t sequence, PendingPresentation &presentation, std::uint64_t now);

        KCDC::Features::Door::DoorService *_door = nullptr;
        KCDC::Features::Player::Combat *_combat  = nullptr;
        std::unordered_map<std::uint64_t, SenderPresentations> _senders;
        std::mutex _incomingMutex;
        std::deque<QueuedPresentation> _incoming;
        std::unordered_map<MafiaNet::PeerGuid, QueuedPresentation> _incomingTerminalRecoveries;
        std::size_t _pendingCount              = 0;
        std::size_t _droppedIncoming           = 0;
        std::size_t _rejectedPending           = 0;
        std::size_t _protectedPendingTerminals = 0;
    };
} // namespace KCDC::Core
