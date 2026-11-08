#include "follow_service.h"
#include <utils/safe_win32.h>

#include "core/application.h"
#include "features/player/player_service.h"
#include "features/ui/ui_action_hints.h"
#include "common/module_support.h"
#include "common/vector_math.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_rider.h"
#include "sdk/wh/entitymodule/c_chat_follow_manager.h"
#include "sdk/wh/entitymodule/c_horse_ride_context.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/entitymodule/e_actor_action_type.h"
#include "sdk/wh/entitymodule/e_actor_state.h"

#include <MinHook.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mafianet/get_time.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

namespace KCDC::Features::Follow {
    namespace {
        /** How far `targets()` looks, and the most a running follow may stretch to. */
        constexpr float kListRadius  = 20.0F;
        constexpr float kLeashRadius = 40.0F;

        /**
         * A body moving faster than a galloping horse with room to spare, or
         * further than the slack in one step, was put there rather than
         * walked there. Scaled by the time since the last sample, so a hitch
         * is not a teleport.
         */
        constexpr float kMaxTravelSpeed = 30.0F;
        constexpr float kTravelSlack    = 5.0F;

        /** How long a queued follow may take to enter before it counts as refused. */
        constexpr std::uint64_t kEntryTimeoutMs = 2000;

        /** The game's Follow key, which the sample's hint shares. */
        constexpr char kFollowSuperaction[]   = "chat_focus_follow_init";
        constexpr char kFollowKeyFilterName[] = "kcdc_scripted_follow";
        constexpr char kFollowKeyFilterXml[]  = "<actionfilter name=\"kcdc_scripted_follow\" type=\"actionFail\">"
                                                "<action name=\"chat_focus_follow_init_press\"/>"
                                                "<action name=\"chat_focus_follow_init_release\"/>"
                                                "<action name=\"focus_follow_init_interactor_press\"/>"
                                                "</actionfilter>";

        /** Nothing else holds this filter, so any reason will do. */
        constexpr std::uint8_t kFollowKeyFilterReason = 0;

        constexpr const char *kMovementActions[] = {"moveforward", "moveback", "moveleft", "moveright", "xi_movex", "xi_movey", "horse_jump", "horse_slowdown", "horse_dismount", "jump"};

        SDK::EntityId HorseOf(SDK::wh::entitymodule::C_Actor *actor) {
            auto *ride = actor != nullptr ? actor->GetRideContext() : nullptr;
            return ride != nullptr && SDK::wh::entitymodule::C_HorseRideContext::IsInSaddle(ride->GetState()) ? ride->GetAction()->GetHorseEntityId() : 0;
        }

        SDK::wh::entitymodule::C_ActorActionFollow *FindFollowAction(SDK::wh::entitymodule::C_Player *local) {
            auto *actions = local->GetActionActor() != nullptr ? local->GetActionActor()->GetActionCollection() : nullptr;
            return actions != nullptr ? static_cast<SDK::wh::entitymodule::C_ActorActionFollow *>(actions->FindActionOfType(SDK::wh::entitymodule::E_ActorActionType::Follow)) : nullptr;
        }
    } // namespace

    FollowService &FollowService::Get() {
        static FollowService service;
        return service;
    }

    void FollowService::AttachResources(Framework::Scripting::ResourceManager *resources) {
        if (resources == nullptr || resources == _resources) {
            return;
        }
        resources->AddOnResourceStopped([](const std::string &resource) {
            FollowService::Get().Release(resource, "resourceStopped");
        });
        _resources = resources;
    }

    SDK::wh::entitymodule::C_Actor *FollowService::Resolve(Target &target) const {
        if (target.npc) {
            return Resolve(target, KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(target.id));
        }
        return Resolve(target, KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(target.id));
    }

    SDK::wh::entitymodule::C_Actor *FollowService::Resolve(Target &target, KCDC::Features::Npc::Npc *npc) const {
        if (npc == nullptr || !npc->alive) {
            return nullptr;
        }
        target.name        = npc->name.empty() ? "NPC" : npc->name;
        target.incarnation = npc->spawnEpoch;
        return Accept(target, npc->ResolveActor());
    }

    SDK::wh::entitymodule::C_Actor *FollowService::Resolve(Target &target, KCDC::Features::Player::Human *human) const {
        if (human == nullptr || human->IsOwner()) {
            return nullptr;
        }
        target.name        = human->nickname;
        target.incarnation = human->poseIncarnation;
        return Accept(target, human->ResolveActor());
    }

    SDK::wh::entitymodule::C_Actor *FollowService::Accept(Target &target, SDK::wh::entitymodule::C_Actor *actor) const {
        if (actor == nullptr || actor->IsDead() || actor->GetSoul() == nullptr || actor->GetSoul()->IsDead() || actor->GetSoul()->IsUnconscious() || actor->GetEntity() == nullptr) {
            return nullptr;
        }
        if (actor->GetActorClass() != SDK::ActorClass_NPC && actor->GetActorClass() != SDK::ActorClass_Player) {
            return nullptr;
        }
        target.entityId = actor->GetEntityId();
        target.position = KCDC::Common::ToGlm(actor->GetEntity()->GetWorldPos());
        return actor;
    }

    std::vector<Target> FollowService::Targets() const {
        std::vector<Target> result;
        auto *replication = Framework::CoreModules::GetReplication();
        auto *local       = SDK::wh::entitymodule::C_Player::GetInstance();
        if (replication == nullptr || local == nullptr || local->GetEntity() == nullptr) {
            return result;
        }
        const auto position = KCDC::Common::ToGlm(local->GetEntity()->GetWorldPos());
        replication->ForEach<KCDC::Features::Player::Human>([&](KCDC::Features::Player::Human *human) {
            Target target;
            target.id = human->GetNetworkID();
            if (Resolve(target, human) != nullptr && glm::distance(position, target.position) <= kListRadius) {
                result.push_back(std::move(target));
            }
        });
        replication->ForEach<KCDC::Features::Npc::Npc>([&](KCDC::Features::Npc::Npc *npc) {
            Target target;
            target.id  = npc->GetNetworkID();
            target.npc = true;
            if (Resolve(target, npc) != nullptr && glm::distance(position, target.position) <= kListRadius) {
                result.push_back(std::move(target));
            }
        });
        return result;
    }

    bool FollowService::Eligible(const Target &target, bool checkDistance) {
        auto *local = SDK::wh::entitymodule::C_Player::GetInstance();
        if (local == nullptr || local->IsDead() || local->GetEntity() == nullptr) {
            return false;
        }
        auto *manager = local->GetChatFollowManager();
        if (manager == nullptr) {
            return false;
        }
        const auto position = KCDC::Common::ToGlm(local->GetEntity()->GetWorldPos());
        if (!KCDC::Common::IsFinite(position) || !KCDC::Common::IsFinite(target.position) || glm::distance(position, target.position) > (checkDistance ? kListRadius : kLeashRadius)) {
            return false;
        }

        // The hook supplies only the missing followable side effect. The
        // native still checks combat, death, interactions and mount state.
        const auto previous = _queryEntity;
        _queryEntity        = target.entityId;
        const bool eligible = manager->IsFollowTargetEligible(target.entityId) && (!checkDistance || manager->MeetsFollowDistanceRequirement(target.entityId));
        _queryEntity        = previous;
        return eligible;
    }

    SDK::wh::entitymodule::C_Actor *FollowService::Startable(Target &target) {
        auto *local = SDK::wh::entitymodule::C_Player::GetInstance();
        if (_action != nullptr || local == nullptr || FindFollowAction(local) != nullptr) {
            return nullptr;
        }
        auto *actor = Resolve(target);
        return actor != nullptr && Eligible(target, true) ? actor : nullptr;
    }

    bool FollowService::CanStart(const Target &target) {
        Target resolved = target;
        return Startable(resolved) != nullptr;
    }

    bool FollowService::Start(const Target &target, const std::string &resource) {
        if (resource.empty()) {
            return false;
        }
        Target resolved = target;
        auto *actor     = Startable(resolved);
        if (actor == nullptr) {
            return false;
        }
        auto *local  = SDK::wh::entitymodule::C_Player::GetInstance();
        _queryEntity = resolved.entityId;
        local->GetChatFollowManager()->StartFollowing(resolved.entityId);
        _queryEntity = 0;
        auto *action = FindFollowAction(local);
        if (action == nullptr) {
            return false;
        }
        action->AddRef();
        _action      = action;
        _target      = resolved;
        _resource    = resource;
        _localEntity = local->GetEntityId();
        _localHorse  = HorseOf(local);
        _targetHorse = HorseOf(actor);
        _requestedAt = MafiaNet::GetTimeMS();
        _sampledAt   = _requestedAt;
        _lastLocal   = KCDC::Common::ToGlm(local->GetEntity()->GetWorldPos());
        _lastTarget  = resolved.position;
        _manualInput.store(false);
        return true;
    }

    bool FollowService::OwnsNativeAction() const {
        auto *local = SDK::wh::entitymodule::C_Player::GetInstance();
        return _action != nullptr && local != nullptr && local->GetEntityId() == _localEntity && FindFollowAction(local) == _action;
    }

    void FollowService::Stop(const char *reason) {
        if (_action == nullptr) {
            return;
        }
        auto *action    = _action;
        const bool owns = OwnsNativeAction();
        _action         = nullptr;
        _active         = false;
        _resource.clear();
        _notices.push_back({_target, reason, false});
        _target = {};

        // Same finish request as the native follow key: Exit unregisters
        // its movement adapter. Keep our reference until that has completed.
        if (owns) {
            action->RequestFinish(false, false);
        }
        action->Release();
    }

    void FollowService::Release(const std::string &resource, const char *reason) {
        if (_action != nullptr && _resource == resource) {
            Stop(reason);
        }
    }

    void FollowService::OnAction(const SDK::CCryName &action, int mode, float value) {
        if (mode == 2 || std::abs(value) < 0.15F) {
            return;
        }
        const char *name = action.c_str();
        for (const char *movement : kMovementActions) {
            if (std::strcmp(name, movement) == 0) {
                _manualInput.store(true);
                return;
            }
        }
    }

    void FollowService::SuppressNativeFollowKey(bool suppress) {
        auto *manager = SDK::IActionMapManager::GetInstance();
        if (manager != _followKeyManager) {
            // A level change replaces the manager, and its filters with it.
            _followKeyManager    = manager;
            _followKeyFilter     = nullptr;
            _followKeySuppressed = false;
        }
        if (manager == nullptr || suppress == _followKeySuppressed) {
            return;
        }
        if (_followKeyFilter == nullptr) {
            _followKeyFilter = manager->FindOrCreateActionFilter(kFollowKeyFilterName, kFollowKeyFilterXml, sizeof(kFollowKeyFilterXml) - 1);
            if (_followKeyFilter == nullptr) {
                return;
            }
        }
        if (suppress) {
            _followKeyFilter->Enable(kFollowKeyFilterReason);
        }
        else {
            _followKeyFilter->Disable(kFollowKeyFilterReason, true);
        }
        _followKeySuppressed = suppress;
    }

    void FollowService::Update() {
        if (_contextChanged.exchange(false)) {
            Stop("worldChanged");
        }
        SuppressNativeFollowKey(KCDC::Features::UI::ActionHints::Get().IsShown(kFollowSuperaction));
        auto *input = SDK::IActionMapManager::GetInstance();
        if (input != nullptr && _inputManager != input && input->AddExtraActionListener(this, nullptr)) {
            _inputManager = input;
        }
        if (_action != nullptr) {
            auto *local     = SDK::wh::entitymodule::C_Player::GetInstance();
            Target resolved = _target;
            auto *actor     = Resolve(resolved);
            if (local == nullptr || local->GetEntityId() != _localEntity || actor == nullptr || resolved.entityId != _target.entityId || resolved.incarnation != _target.incarnation) {
                Stop("targetLost");
            }
            else if (_manualInput.exchange(false)) {
                Stop("input");
            }
            else if (HorseOf(local) != _localHorse || HorseOf(actor) != _targetHorse) {
                Stop("mountChanged");
            }
            else {
                const auto now      = MafiaNet::GetTimeMS();
                const auto position = KCDC::Common::ToGlm(local->GetEntity()->GetWorldPos());
                const float reach   = kTravelSlack + kMaxTravelSpeed * static_cast<float>(now - _sampledAt) / 1000.0F;
                if (glm::distance(position, _lastLocal) > reach || glm::distance(resolved.position, _lastTarget) > reach) {
                    Stop("teleported");
                }
                else if (!Eligible(resolved, false)) {
                    Stop("unavailable");
                }
                else if (!OwnsNativeAction() || (_active && !_action->IsActive())) {
                    Stop("interrupted");
                }
                else if (!_active && _action->IsActive()) {
                    _active = true;
                    _notices.push_back({resolved, {}, true});
                }
                else if (!_active && now - _requestedAt > kEntryTimeoutMs) {
                    Stop("failed");
                }
                _sampledAt  = now;
                _lastLocal  = position;
                _lastTarget = resolved.position;
                if (_action != nullptr) {
                    _target = resolved;
                }
            }
        }
        DrainNotices();
    }

    void FollowService::Reset() {
        Stop("sessionEnded");
        DrainNotices();
        SuppressNativeFollowKey(false);
        if (_inputManager != nullptr && SDK::IActionMapManager::GetInstance() == _inputManager) {
            _inputManager->RemoveExtraActionListener(this, nullptr);
        }
        _inputManager = nullptr;
        _manualInput.store(false);
    }

    void FollowService::NativeContextChanged() {
        _contextChanged.store(true);
    }

    SDK::wh::entitymodule::S_FollowTargetMode *FollowService::Classify(void *context, SDK::wh::entitymodule::S_FollowTargetMode *out, SDK::EntityId entity) {
        auto *result = _classifyOriginal(context, out, entity);

        // Only a target the game turned down, and only ours: a side effect
        // it does carry keeps its own left/right slot.
        if (result->valid || entity == 0 || (entity != _queryEntity && (_action == nullptr || entity != _target.entityId))) {
            return result;
        }
        auto *actors = SDK::IActorSystem::GetInstance();
        auto *actor  = actors != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actors->GetActor(entity)) : nullptr;
        auto *soul   = actor != nullptr ? actor->GetSoul() : nullptr;
        if (soul != nullptr && !soul->IsDead() && !soul->IsUnconscious()) {
            result->mode  = 0;
            result->valid = true;
        }
        return result;
    }

    SDK::wh::entitymodule::S_FollowTargetMode *FollowService::ClassifyHook(void *context, SDK::wh::entitymodule::S_FollowTargetMode *out, SDK::EntityId entity) {
        return FollowService::Get().Classify(context, out, entity);
    }

    void FollowService::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ClassifyFollowTarget), reinterpret_cast<LPVOID>(&ClassifyHook), reinterpret_cast<LPVOID *>(&FollowService::Get()._classifyOriginal));
    }
} // namespace KCDC::Features::Follow

static InitFunction init(
    []() {
        KCDC::Features::Follow::FollowService::Install();
    },
    "Follow");
