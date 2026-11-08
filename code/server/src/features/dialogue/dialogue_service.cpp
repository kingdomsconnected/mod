#include "features/dialogue/dialogue_service.h"

#include "features/dialogue/dialogue_scripting.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Dialogue {
    namespace {
        /** The peer behind a player network id, or an invalid guid. */
        bool ResolvePeer(std::uint64_t playerId, MafiaNet::PeerGuid &guid) {
            auto *replication = Framework::CoreModules::GetReplication();
            if (replication == nullptr) {
                return false;
            }
            auto *player = replication->GetEntity<Shared::Entities::HumanEntity>(playerId);
            if (player == nullptr) {
                return false;
            }
            guid = player->ownerGUID;
            return true;
        }
    } // namespace

    DialogueService &DialogueService::Get() {
        static DialogueService instance;
        return instance;
    }

    void DialogueService::Install() {
        if (_installed) {
            return;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        _installed = true;

        net->RegisterRPC<Shared::RPC::DialogueChoice>([](const Shared::RPC::DialogueChoice &choice, MafiaNet::Packet *packet) {
            auto *replication = Framework::CoreModules::GetReplication();
            if (packet == nullptr || !choice.IsSane() || replication == nullptr) {
                return;
            }
            const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(packet->guid);
            auto *player                  = replication->GetViewerAs<Shared::Entities::HumanEntity>(guid);
            if (player == nullptr) {
                return;
            }

            DialogueService &self = DialogueService::Get();
            Session *session      = self.Find(choice.sessionId);
            // Three gates, and each one is a real case rather than defensive
            // noise: a session that ended, a session belonging to someone else,
            // and an answer to a page that has already been replaced.
            if (session == nullptr || session->playerId != player->GetNetworkID() || session->pageId != choice.pageId) {
                return;
            }
            if (choice.optionIndex >= session->page.options.size()) {
                return;
            }
            const Option &option = session->page.options[choice.optionIndex];
            if (!option.enabled) {
                // `enabled` is a render hint on the wire; the server decides.
                return;
            }
            Scripting::Events::DialogueChoice(choice.sessionId, player->GetNetworkID(), option.id);
        });

        net->RegisterRPC<Shared::RPC::DialogueClose>([](const Shared::RPC::DialogueClose &message, MafiaNet::Packet *packet) {
            auto *replication = Framework::CoreModules::GetReplication();
            if (packet == nullptr || !message.IsSane() || replication == nullptr) {
                return;
            }
            const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(packet->guid);
            auto *player                  = replication->GetViewerAs<Shared::Entities::HumanEntity>(guid);
            if (player == nullptr) {
                return;
            }
            DialogueService &self = DialogueService::Get();
            Session *session      = self.Find(message.sessionId);
            if (session == nullptr || session->playerId != player->GetNetworkID()) {
                return;
            }
            const std::uint64_t playerId = session->playerId;
            self._sessions.erase(message.sessionId);
            self._byPlayer.erase(playerId);
            Scripting::Events::DialogueClosed(message.sessionId, playerId, message.reason);
        });
    }

    DialogueService::Session *DialogueService::Find(std::uint32_t sessionId) {
        auto it = _sessions.find(sessionId);
        return it == _sessions.end() ? nullptr : &it->second;
    }

    std::uint32_t DialogueService::SessionOf(std::uint64_t playerId) const {
        auto it = _byPlayer.find(playerId);
        return it == _byPlayer.end() ? 0 : it->second;
    }

    bool DialogueService::Send(const Session &session, std::uint32_t sessionId) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        MafiaNet::PeerGuid guid {};
        if (net == nullptr || !ResolvePeer(session.playerId, guid)) {
            return false;
        }
        Shared::RPC::DialogueShowPage message;
        message.sessionId   = sessionId;
        message.pageId      = session.pageId;
        message.onRight     = session.page.onRight;
        message.line        = session.page.line;
        message.optionCount = static_cast<std::uint8_t>(session.page.options.size());
        for (std::size_t index = 0; index < session.page.options.size(); ++index) {
            message.optionText[index]    = session.page.options[index].text;
            message.optionEnabled[index] = session.page.options[index].enabled;
        }
        net->SendRPC(message, MafiaNet::ToGuid(guid), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    std::uint32_t DialogueService::Open(std::uint64_t playerId, const Page &page) {
        if (page.options.empty() || page.options.size() > Shared::RPC::kMaxDialogueOptions) {
            return 0;
        }
        // One conversation per player. Replacing rather than stacking keeps the
        // client's single HUD list honest, and the old session's owner is told.
        if (const std::uint32_t existing = SessionOf(playerId); existing != 0) {
            Close(existing, Shared::RPC::DialogueCloseReason::Replaced);
        }

        const std::uint32_t sessionId = _nextSession++;
        if (_nextSession == 0) {
            _nextSession = 1;
        }
        Session session;
        session.playerId = playerId;
        session.pageId   = 1;
        session.page     = page;

        if (!Send(session, sessionId)) {
            return 0;
        }
        _sessions[sessionId] = std::move(session);
        _byPlayer[playerId]  = sessionId;
        return sessionId;
    }

    bool DialogueService::Update(std::uint32_t sessionId, const Page &page) {
        Session *session = Find(sessionId);
        if (session == nullptr || page.options.empty() || page.options.size() > Shared::RPC::kMaxDialogueOptions) {
            return false;
        }
        // A new generation: any answer still in flight against the old page is
        // now stale and will be dropped when it arrives.
        ++session->pageId;
        session->page = page;
        return Send(*session, sessionId);
    }

    bool DialogueService::Close(std::uint32_t sessionId, Shared::RPC::DialogueCloseReason reason) {
        Session *session = Find(sessionId);
        if (session == nullptr) {
            return false;
        }
        const std::uint64_t playerId = session->playerId;

        auto *net = Framework::CoreModules::GetNetworkPeer();
        MafiaNet::PeerGuid guid {};
        if (net != nullptr && ResolvePeer(playerId, guid)) {
            Shared::RPC::DialogueClose message;
            message.sessionId = sessionId;
            message.reason    = static_cast<std::uint8_t>(reason);
            net->SendRPC(message, MafiaNet::ToGuid(guid), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }

        _sessions.erase(sessionId);
        _byPlayer.erase(playerId);
        Scripting::Events::DialogueClosed(sessionId, playerId, static_cast<std::uint8_t>(reason));
        return true;
    }

    void DialogueService::Forget(std::uint64_t playerId) {
        const std::uint32_t sessionId = SessionOf(playerId);
        if (sessionId == 0) {
            return;
        }
        _sessions.erase(sessionId);
        _byPlayer.erase(playerId);
        // No message: the peer this would go to is the one that just left.
        Scripting::Events::DialogueClosed(sessionId, playerId, static_cast<std::uint8_t>(Shared::RPC::DialogueCloseReason::Interrupted));
    }
} // namespace KCDC::Features::Dialogue
