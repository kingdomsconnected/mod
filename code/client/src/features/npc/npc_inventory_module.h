#pragma once

#include "core/feature_registry.h"
#include "features/npc/npc.h"
#include "features/player/player_duel_roster.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/wh/entitymodule/c_actor_action_butchering.h"
#include "sdk/wh/entitymodule/c_player_interactor.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"
#include "shared/features/npc/npc_inventory_message.h"

#include <atomic>
#include <map>
#include <mutex>
#include <unordered_map>

namespace KCDC::Features::Npc {
    /** The local corpse screen, held native prompts and their server grants. */
    class NpcInventoryModule final: public KCDC::Core::IFeatureService {
      public:
        static NpcInventoryModule &Get();
        static void Install();
        void Update() override;
        void Reset() override;
        void NativeContextChanged() override {
            _contextChanged = true;
        }
        void Seed(Npc &npc);
        void ReleaseBody(Npc &npc);
        bool RequestExchange(SDK::wh::entitymodule::C_Actor &actor, SDK::EntityId user);
        void RegisterAction(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::S_InteractorActionRecord &record, SDK::HScriptFunction function);
        bool InterceptAction(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation);
        bool StartButchering(SDK::wh::entitymodule::C_ActorActionButchering &action);
        void EnteredButchering(const SDK::wh::entitymodule::C_ActorActionButchering &action, bool accepted);
        bool CompletedButchering(SDK::wh::entitymodule::C_ActorActionButchering &action);
        void ExitedButchering(const SDK::wh::entitymodule::C_ActorActionButchering &action, bool cancelled);
        bool RequestTransfer(SDK::wh::entitymodule::C_Inventory *destination, const SDK::StdVector<SDK::wh::entitymodule::C_Item *> &items, std::uint32_t amount);
        bool ManagesHolder(const SDK::wh::entitymodule::C_ItemHolder *holder) const;
        bool ManagesItem(const SDK::wh::entitymodule::C_Item *item) const;
        void NotifyNativeChange(const SDK::wh::entitymodule::C_ItemHolder *holder);
        bool AnswerCanBeButchered(const SDK::wh::rpgmodule::I_Soul *soul, bool &answer) const;

      private:
        enum class Stage {
            Idle,
            Requesting,
            Projecting,
            Harvesting,
            Completing,
            Open
        };
        struct Session {
            std::uint64_t npc                                            = 0;
            SDK::EntityId entity                                         = 0;
            std::uint32_t epoch                                          = 0;
            std::uint64_t token                                          = 0;
            std::uint32_t request                                        = 0;
            std::uint32_t renewal                                        = 0;
            KCDC::Shared::NpcInventory::Action action                    = KCDC::Shared::NpcInventory::Action::Open;
            Stage stage                                                  = Stage::Idle;
            std::uint64_t deadline                                       = 0;
            std::uint64_t renewAt                                        = 0;
            std::uint64_t playerRevision                                 = 0;
            SDK::HScriptFunction function                                = 0;
            SDK::IScriptSystem *scripts                                  = nullptr;
            SDK::wh::entitymodule::C_ActorActionButchering *nativeAction = nullptr;
            bool completed                                               = false;
            bool cancelled                                               = false;
            bool wantOpen                                                = false;
            bool opening                                                 = false;
            bool screenOpened                                            = false;
            std::uint64_t toolWuid                                       = 0;
            bool transferPending                                         = false;
            bool deposit                                                 = false;
            std::map<std::string, std::uint32_t> units;
        };
        struct SeedRequest {
            std::uint32_t request;
            std::uint32_t epoch;
            std::uint64_t deadline;
        };
        struct Holders {
            std::vector<const SDK::wh::entitymodule::C_ItemHolder *> inventories;
        };
        static Npc *FindBody(SDK::EntityId entity);
        static Npc *Find(std::uint64_t id);
        static bool CanUse(const Npc &npc);
        bool Busy(Npc &npc) const;
        std::uint32_t Send(KCDC::Shared::NpcInventory::Action action, std::uint64_t npc, std::uint32_t epoch, std::uint64_t token, nlohmann::json fields = nlohmann::json::object());
        void Begin(Npc &npc, KCDC::Shared::NpcInventory::Action action);
        void End(bool closeScreen = true);
        void QueueReply(const KCDC::Shared::RPC::NpcInventoryReply &reply);
        void ApplyReply(const nlohmann::json &reply);
        void FlushTransfer(Npc &npc);
        void ReleaseFunction();
        Session _session;
        std::uint32_t _nextRequest = 1;
        bool _replaying            = false;
        std::unordered_map<SDK::EntityId, SDK::HScriptFunction> _butcherFunctions;
        std::unordered_map<std::uint64_t, SeedRequest> _seeds;
        KCDC::Features::Player::RosterRing<Holders, 4> _holders;
        std::atomic<bool> _contextChanged {false};
        std::mutex _incomingMutex;
        std::vector<nlohmann::json> _incoming;
        std::vector<const SDK::wh::entitymodule::C_ItemHolder *> _changedHolders;
    };
} // namespace KCDC::Features::Npc
