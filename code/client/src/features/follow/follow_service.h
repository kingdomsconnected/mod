#pragma once

#include <utils/safe_win32.h>

#include "core/feature_registry.h"
#include "features/npc/npc.h"
#include "features/player/player.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_follow.h"
#include "sdk/wh/entitymodule/s_follow_target_mode.h"

#include <atomic>
#include <cstdint>
#include <glm/vec3.hpp>
#include <scripting/resource/resource_manager.h>
#include <string>
#include <vector>

namespace KCDC::Features::Follow {
    struct Target {
        std::uint64_t id = 0;
        bool npc         = false;
        std::string name;
        glm::vec3 position {};
        SDK::EntityId entityId    = 0;
        std::uint32_t incarnation = 0;
    };

    /** Owns only this resource's local native follow action. Game thread, apart from the input flags. */
    class FollowService final
        : public KCDC::Core::IFeatureService
        , public SDK::IActionListener {
      public:
        static FollowService &Get();
        static void Install();

        /** Stops a resource's follow when the resource stops. Once per manager. */
        void AttachResources(Framework::Scripting::ResourceManager *resources);

        std::vector<Target> Targets() const;
        bool CanStart(const Target &target);
        bool Start(const Target &target, const std::string &resource);
        void Stop(const char *reason = "script");

        /** Stops the follow only when `resource` owns it. */
        void Release(const std::string &resource, const char *reason);

        const Target *Current() const {
            return _active ? &_target : nullptr;
        }

        bool Pending() const {
            return _action != nullptr && !_active;
        }

        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;
        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

      private:
        struct Notice {
            Target target;
            std::string reason;
            bool started;
        };

        SDK::wh::entitymodule::C_Actor *Resolve(Target &target) const;
        SDK::wh::entitymodule::C_Actor *Resolve(Target &target, KCDC::Features::Player::Human *human) const;
        SDK::wh::entitymodule::C_Actor *Resolve(Target &target, KCDC::Features::Npc::Npc *npc) const;
        SDK::wh::entitymodule::C_Actor *Accept(Target &target, SDK::wh::entitymodule::C_Actor *actor) const;

        /** The target's actor when the local player could start following it now, else null. Refreshes `target`. */
        SDK::wh::entitymodule::C_Actor *Startable(Target &target);
        bool Eligible(const Target &target, bool checkDistance);
        bool OwnsNativeAction() const;
        void DrainNotices();

        /** Turns the game's own Follow key off while a hint sits on it, so one press is not handled twice. */
        void SuppressNativeFollowKey(bool suppress);

        SDK::wh::entitymodule::S_FollowTargetMode *Classify(void *context, SDK::wh::entitymodule::S_FollowTargetMode *out, SDK::EntityId entity);
        static SDK::wh::entitymodule::S_FollowTargetMode *ClassifyHook(void *context, SDK::wh::entitymodule::S_FollowTargetMode *out, SDK::EntityId entity);

        SDK::wh::entitymodule::S_FollowTargetMode *(__fastcall *_classifyOriginal)(void *, SDK::wh::entitymodule::S_FollowTargetMode *, SDK::EntityId) = nullptr;
        SDK::wh::entitymodule::C_ActorActionFollow *_action                                                                                            = nullptr;
        SDK::EntityId _localEntity                                                                                                                     = 0;
        SDK::EntityId _queryEntity                                                                                                                     = 0;
        SDK::EntityId _localHorse                                                                                                                      = 0;
        SDK::EntityId _targetHorse                                                                                                                     = 0;
        Target _target;
        std::string _resource;
        std::vector<Notice> _notices;
        bool _active               = false;
        std::uint64_t _requestedAt = 0;
        std::uint64_t _sampledAt   = 0;
        glm::vec3 _lastLocal {};
        glm::vec3 _lastTarget {};
        std::atomic<bool> _manualInput {false};
        std::atomic<bool> _contextChanged {false};
        SDK::IActionMapManager *_inputManager             = nullptr;
        SDK::IActionMapManager *_followKeyManager         = nullptr;
        SDK::IActionFilter *_followKeyFilter              = nullptr;
        bool _followKeySuppressed                         = false;
        Framework::Scripting::ResourceManager *_resources = nullptr;
    };
} // namespace KCDC::Features::Follow
