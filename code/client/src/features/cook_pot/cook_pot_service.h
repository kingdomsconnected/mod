#pragma once

#include "core/feature_registry.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_player_interactor.h"
#include "shared/features/cook_pot/cook_pot_message.h"
#include "shared/features/cook_pot/cook_pot_state.h"
#include "shared/game/cook_pot_catalog.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::CookPot {
    /**
     * Server-owned stew pots, wired into the game's own kettle logic.
     *
     * Three native seams, no script of our own:
     * - the interaction gate (`AddLuaAction` / `InvokeLuaActionFunc`) disables
     *   prompts the server would refuse and holds an eat press until the
     *   server reserves a portion, then replays it through the game's own
     *   `KettleActionTrigger:ReportUse`;
     * - the fireplace brain's `persistentStorage.state` is kept edible or
     *   empty to match the server, so the game's `CanEat` agrees with it;
     * - `Game.SetPrefabPhaseById`, the only way so_fireplace.xml changes a
     *   cauldron's look or lights its fire and steam, is rewritten to the
     *   server's phase, and is where any local AI change of state is put back;
     * - a fire the server set is sent to the brain as the
     *   `kettle:signal:onFireplacePhaseChange` quests light it with, so its
     *   smoke and burn-out timer follow, and sent again whenever the local AI
     *   turns it the other way.
     */
    class CookPotService final: public KCDC::Core::IFeatureService {
      public:
        static CookPotService &Get();
        static void Install();
        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

        /** From any thread: a pot replica arrived, changed or left. */
        void MarkStockChanged() {
            _stockChanged = true;
        }

        /** Interaction gate: disables a pot prompt the server would refuse, and remembers what its handler is. */
        void OnActionRegistering(const SDK::wh::entitymodule::C_PlayerInteractor &interactor, SDK::wh::entitymodule::S_InteractorActionRecord &record, SDK::HScriptFunction function);

        /** Interaction gate: true when a pot press is held for the server or refused outright. */
        bool InterceptInteraction(const SDK::wh::entitymodule::S_LuaActionInvocation &invocation);

        /** The phase a cauldron, fire or steam prefab should take when the game sets `requested`; re-asserts the pot's edibility on the way. */
        int OverridePhase(SDK::EntityId prefab, const char *category, int requested);

        bool AllowsFoodEffects() const {
            return !_replaying || _foodEffects;
        }
        bool IsReplayingFood(const SDK::wh::entitymodule::C_Item *item) const;
        void FoodUseFinished(bool eaten) {
            if (_replaying) {
                _ate = eaten;
            }
        }

      private:
        enum class Handler : std::uint8_t {
            Eat,
            Refused
        };

        struct Prompt {
            Handler handler;
            const KCDC::Shared::Game::CookPotPlacement *placement;
        };

        struct Pending {
            KCDC::Shared::RPC::CookPotRequest request;
            SDK::HScriptFunction function                         = 0;
            SDK::EntityId trigger                                 = 0;
            const KCDC::Shared::Game::CookPotPlacement *placement = nullptr;
            std::uint64_t deadline                                = 0;
        };

        bool RefreshLevel();
        void RescanStock();
        void Apply(const KCDC::Shared::Game::CookPotPlacement &placement, bool forceEdible = false) const;
        void SyncState(const KCDC::Shared::Game::CookPotPlacement &placement, bool edible) const;
        /** Tells each stale fireplace's brain the server's fire, as a quest lights one. */
        void SyncFires();
        const KCDC::Shared::CookPot::Stock *StockOf(const KCDC::Shared::Game::CookPotPlacement &placement) const;
        bool IsStewTrigger(SDK::IEntity *entity);
        void Settle(const KCDC::Shared::RPC::CookPotReply &reply);
        bool SyncFood(const KCDC::Shared::Game::CookPotPlacement &placement, KCDC::Shared::Game::CookPotFood food) const;
        void Drop();

        std::mutex _mutex;
        std::vector<KCDC::Shared::RPC::CookPotReply> _incoming;
        KCDC::Shared::RPC::CookPotContext _serverContext;
        KCDC::Shared::RPC::CookPotContext _context;
        std::atomic<bool> _stockChanged {true};
        std::atomic<bool> _levelChanged {true};

        std::string _level;
        std::unordered_map<std::uint64_t, const KCDC::Shared::Game::CookPotPlacement *> _byTrigger;
        std::unordered_map<std::uint64_t, const KCDC::Shared::Game::CookPotPlacement *> _byCauldron;
        // Fire and steam prefabs, both switched by the server's fire.
        std::unordered_map<std::uint64_t, const KCDC::Shared::Game::CookPotPlacement *> _byFire;
        // Pots whose brain must be told the server's fire on the next update.
        std::unordered_set<std::uint64_t> _fireStale;
        std::unordered_map<std::uint64_t, KCDC::Shared::CookPot::Stock> _stock;
        std::unordered_map<SDK::HScriptFunction, Prompt> _prompts;
        SDK::IEntityClass *_kettleClass = nullptr;

        Pending _pending;
        bool _waiting           = false;
        bool _replaying         = false;
        bool _foodEffects                           = true;
        bool _ate                                   = false;
        KCDC::Shared::Game::CookPotFood _replayFood = KCDC::Shared::Game::CookPotFood::Goulash;
        std::uint64_t _sequence = 0;
    };
} // namespace KCDC::Features::CookPot
