#include <utils/safe_win32.h>

#include "social_emote_service.h"

#include "sdk/cryengine/action/i_mannequin_action.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "core/application.h"
#include "features/player/player.h"
#include "shared/features/social/social_emote_sequence.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace KCDC::Features::Social {
    namespace {
        // Kept by pointer by the engine's own MCM diagnostics, so it has to
        // outlive every call that states it.
        constexpr const char *kMovementLockReason = "KCDCEmote";
    } // namespace

    void EmoteService::Release(Playback &playback, bool cancel) {
        auto *action = playback.action;
        if (action == nullptr) {
            return;
        }
        // ForceFinish defers Exit, so report cancellation first.
        if (cancel && &playback == &_local && KCDC::Core::gApplication != nullptr) {
            NotifyActionExited(action);
        }
        playback = {};
        if (cancel) {
            action->ForceFinish();
        }
        action->Release();
    }

    namespace {
        /**
         * Builds the Mannequin action one emote row describes, or null.
         *
         * The resolution is the same whoever the body belongs to -- find the
         * fragment by CRC, set the row's tag when it names one, create the
         * action -- so the managed path and the unmanaged one both come through
         * here rather than each spelling it out.
         */
        SDK::IAction *CreateEmoteAction(SDK::wh::entitymodule::C_Actor *actor, std::uint16_t id, SDK::IActionController *&controllerOut) {
            const auto *entry = Shared::Game::FindEmote(id);
            if (entry == nullptr || actor == nullptr || !actor->IsSettled()) {
                return nullptr;
            }
            auto *animated   = actor->GetAnimatedCharacterRuntime();
            auto *controller = animated != nullptr ? animated->GetActionController() : nullptr;
            if (controller == nullptr || animated->GetBoundCharacter3P() == nullptr) {
                return nullptr;
            }
            const auto fragment = controller->FindFragment(entry->fragmentCrc);
            if (fragment == SDK::CTagDefinition::kInvalidId) {
                return nullptr;
            }
            SDK::SFragTagState tags;
            if (entry->tag[0] != '\0') {
                const auto *definition = controller->GetFragmentTagDefinition(fragment);
                const auto tag         = definition != nullptr ? definition->FindCRC(entry->tagCrc) : SDK::CTagDefinition::kInvalidId;
                if (tag == SDK::CTagDefinition::kInvalidId) {
                    return nullptr;
                }
                definition->SetTag(tags.bits, tag, true);
            }
            auto *action = SDK::TAction<SDK::SAnimationContext>::Create(fragment, tags, SDK::IAction::kPlayAnimPriority);
            if (action != nullptr) {
                controllerOut = controller;
            }
            return action;
        }
    } // namespace

    bool EmoteService::Play(Playback &playback, SDK::wh::entitymodule::C_Actor *actor, std::uint16_t id) {
        SDK::IActionController *controller = nullptr;
        auto *action                       = CreateEmoteAction(actor, id, controller);
        if (action == nullptr) {
            return false;
        }
        Release(playback, true);
        action->AddRef();          // our reference; Queue takes its own
        action->SetOptionIndex(0); // same variant on every peer
        playback = {action, id, false, false, MafiaNet::GetTime() + 1000};
        controller->Queue(*action, 0.5F);
        return true;
    }

    bool EmoteService::QueueEmoteOnActor(SDK::wh::entitymodule::C_Actor *actor, std::uint16_t emoteId) {
        SDK::IActionController *controller = nullptr;
        auto *action                       = CreateEmoteAction(actor, emoteId, controller);
        if (action == nullptr) {
            return false;
        }
        action->SetOptionIndex(0); // same variant on every peer
        // Queue takes its own reference and owns the action from here, which is
        // what forgetting it means: nothing is left holding a pointer that would
        // have to be released later.
        controller->Queue(*action, 0.5F);
        return true;
    }

    void EmoteService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        net->RegisterRPC<Shared::RPC::Emote>([](const Shared::RPC::Emote &event, MafiaNet::Packet *) {
            if (!event.IsSane() || event.actorNetworkId == 0 || KCDC::Core::gApplication == nullptr) {
                return;
            }
            KCDC::Core::gApplication->GetEmoteService().Receive(event);
        });
    }

    void EmoteService::Receive(const Shared::RPC::Emote &event) {
        std::lock_guard lock(_inboxMutex);
        if (_receiving && _inbox.size() < 256) {
            _inbox.push_back(event);
        }
    }

    void EmoteService::RequestLocal(std::uint16_t emoteId) {
        if (_sessionActive.load() && (emoteId == 0 || Shared::Game::FindEmote(emoteId) != nullptr)) {
            _command.store(emoteId);
        }
    }

    std::uint16_t EmoteService::ActionEntered(const SDK::IAction *action) {
        if (action != nullptr && action == _local.action) {
            _local.entered = true;
            return _local.emoteId;
        }
        for (auto &[id, remote] : _remote) {
            if (action == remote.playback.action) {
                remote.playback.entered = true;
            }
        }
        return 0;
    }

    void EmoteService::ActionExited(const SDK::IAction *action) {
        if (action == _local.action) {
            _local.ended = true;
        }
        for (auto &[id, remote] : _remote) {
            if (action == remote.playback.action) {
                remote.playback.ended = true;
            }
        }
    }

    void EmoteService::NotifyActionEntered(SDK::IAction *action) {
        const auto id = ActionEntered(action);
        auto *net     = Framework::CoreModules::GetNetworkPeer();
        if (id == 0 || action == _reportedEmoteAction || net == nullptr || _nextEmoteSequence == 0) {
            return;
        }
        _reportedEmoteAction   = action;
        _reportedEmoteId       = id;
        _reportedEmoteSequence = _nextEmoteSequence++;
        Shared::RPC::Emote report;
        report.sequence = _reportedEmoteSequence;
        report.emoteId  = id;
        net->BroadcastRPC(report);
    }

    void EmoteService::NotifyActionExited(SDK::IAction *action) {
        if (action != nullptr && action == _reportedEmoteAction) {
            Shared::RPC::Emote report;
            report.sequence      = _reportedEmoteSequence;
            report.emoteId       = _reportedEmoteId;
            report.phase         = Shared::RPC::Emote::Stop;
            _reportedEmoteAction = nullptr;
            if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
                net->BroadcastRPC(report);
            }
        }
        ActionExited(action);
    }

    void EmoteService::UpdateLocalMovementLock() {
        auto *player      = SDK::wh::entitymodule::C_Player::GetInstance();
        auto *animated    = player != nullptr ? player->GetAnimatedCharacterRuntime() : nullptr;
        const auto *entry = _local.action != nullptr && _local.entered ? Shared::Game::FindEmote(_local.emoteId) : nullptr;
        auto *wanted      = entry != nullptr && entry->fullBody ? animated : nullptr;
        if (wanted == _localMovementLock) {
            return;
        }
        // A body that has gone away takes its animated character with it, so
        // release only through the one the game still answers with and drop
        // anything else unwritten.
        if (_localMovementLock != nullptr && _localMovementLock == animated) {
            animated->SetMovementControlMethods(SDK::eMCMSlot_Animation, SDK::eMCM_Undefined, SDK::eMCM_Undefined, kMovementLockReason);
        }
        _localMovementLock = wanted;
        if (_localMovementLock != nullptr) {
            _localMovementLock->SetMovementControlMethods(SDK::eMCMSlot_Animation, SDK::eMCM_Animation, SDK::eMCM_Undefined, kMovementLockReason);
        }
    }

    void EmoteService::Update() {
        _sessionActive.store(true);
        std::vector<Shared::RPC::Emote> events;
        {
            std::lock_guard lock(_inboxMutex);
            _receiving = true;
            events.swap(_inbox);
        }
        auto *replication = Framework::CoreModules::GetReplication();
        for (const auto &event : events) {
            auto *human = replication != nullptr ? replication->GetEntity<KCDC::Features::Player::Human>(event.actorNetworkId) : nullptr;
            if (human == nullptr || human->IsOwner()) {
                continue;
            }
            auto &remote = _remote[event.actorNetworkId];
            if (!remote.sequence.Accept(event, MafiaNet::GetTime(), false)) {
                continue;
            }
            if (event.phase == Shared::RPC::Emote::Stop) {
                Release(remote.playback, true);
            }
            else if (!Play(remote.playback, human->ResolveActor(), event.emoteId)) {
                Framework::Logging::GetLogger("Emotes")->debug("Emote {} skipped for unready player {}", event.emoteId, event.actorNetworkId);
            }
        }

        const int command = _command.exchange(-1);
        if (command == 0) {
            Release(_local, true);
        }
        else if (command > 0) {
            bool hasOwner = false;
            if (replication != nullptr) {
                replication->ForEach<KCDC::Features::Player::Human>([&](KCDC::Features::Player::Human *human) { hasOwner = hasOwner || human->IsOwner(); });
            }
            if (hasOwner && !Play(_local, SDK::wh::entitymodule::C_Player::GetInstance(), static_cast<std::uint16_t>(command))) {
                Framework::Logging::GetLogger("Emotes")->warn("Emote {} could not be queued on the local actor", command);
            }
        }
        // Native pending expiry can remove an action without calling Exit.
        const auto now            = MafiaNet::GetTime();
        const auto pendingExpired = [now](const Playback &playback) { return playback.action != nullptr && !playback.entered && now >= playback.pendingDeadline; };
        if (_local.ended || pendingExpired(_local)) {
            Release(_local, !_local.ended);
        }
        for (auto &[id, remote] : _remote) {
            if (remote.playback.ended || pendingExpired(remote.playback)) {
                Release(remote.playback, !remote.playback.ended);
            }
        }
        // Last, so it reads the playback the releases above have settled.
        // Remote bodies need none of this: they are placed from the owner's
        // transform, which has already stopped.
        UpdateLocalMovementLock();
    }

    void EmoteService::ForgetActor(std::uint64_t networkId) {
        const auto found = _remote.find(networkId);
        if (found != _remote.end()) {
            Release(found->second.playback, true);
            _remote.erase(found);
        }
    }

    void EmoteService::Reset() {
        _sessionActive.store(false);
        {
            std::lock_guard lock(_inboxMutex);
            _receiving = false;
            _inbox.clear();
        }
        _command.store(-1);
        Release(_local, true);
        UpdateLocalMovementLock();
        for (auto &[id, remote] : _remote) {
            Release(remote.playback, true);
        }
        _remote.clear();

        // Last, because the releases above are still reporting against it: the
        // local playback's cancellation is what sends this session's final Stop.
        _reportedEmoteAction   = nullptr;
        _nextEmoteSequence     = 1;
        _reportedEmoteSequence = 0;
        _reportedEmoteId       = 0;
    }
} // namespace KCDC::Features::Social
