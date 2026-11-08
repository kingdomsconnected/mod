#pragma once

#include "features/area/area_service.h"
#include "features/cook_pot/cook_pot_service.h"
#include "features/crafting/crafting_service.h"
#include "features/door/door_service.h"
#include "features/door/gate_service.h"
#include "features/gathering/gathering_service.h"
#include "features/ground_item/ground_item_service.h"
#include "features/horse/horse_service.h"
#include "features/inventory/inventory_service.h"
#include "features/navigation/navigation_service.h"
#include "features/npc/npc_verbs.h"
#include "features/player/player_buff_module.h"
#include "features/player/player_combat_module.h"
#include "features/player/player_item_requests.h"
#include "features/player/player_missile_module.h"
#include "features/player/player_rock_module.h"
#include "features/player/player_progression_module.h"
#include "features/player/player_service.h"
#include "features/social/social_emote_service.h"
#include "features/social/social_scoreboard_service.h"
#include "features/stash/stash_service.h"
#include "features/streamed_assets/mod_table_service.h"
#include "features/world/world_query_service.h"
#include "features/world/world_service.h"
#include "features/world_builder/world_builder_service.h"
#include "features/light/light_service.h"
#include "presentation.h"

#include <integrations/server/instance.h>

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC {
    /** One HumanEntity per connection; clients spawn a body for each. */
    class Server final: public Framework::Integrations::Server::Instance {
      public:
        KCDC::Features::CookPot::CookPotService &GetCookPots() { return _cookPots; }
        void PostInit() override;
        /** After the scripting module exists and before any resource starts. */
        void PostScriptInit() override;
        /** Loads `mod.world_resources` once resources are running, so `worldResourcesReady` has listeners. A failure lands in the config error and refuses the run. */
        void LoadWorldResources();
        void PreShutdown() override;
        void PostUpdate() override;
        void OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) override;
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid) override;
        // The spawn barrier opening is the body standing at its spawn.
        void OnPlayerReady(MafiaNet::PeerGuid guid) override;

        void OnChatMessage(uint64_t senderNetworkId, const std::string &text) override;
        void OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command, const std::vector<std::string> &args) override;

        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        /** Hands resources the KCDC `Player`, not the framework's bare one. */
        v8::Local<v8::Value> WrapScriptPlayer(v8::Isolate *isolate, uint64_t networkId) override;

        /**
         * Whether a plain chat line is echoed back to everyone.
         *
         * On by default, so a server with no resources still has working
         * chat. A resource that wants to own delivery -- teams, ranges,
         * moderation -- turns it off and answers `playerChat` itself.
         */
        void SetDefaultChatRelay(bool enabled) {
            _defaultChatRelay = enabled;
        }
        bool IsDefaultChatRelay() const {
            return _defaultChatRelay;
        }

        KCDC::Features::Gathering::GatheringService &GetGathering() {
            return _gathering;
        }

        /** First-claim authority over replicated stacks, and the identity the server mints for the ones it authors itself. */
        KCDC::Features::GroundItem::GroundItemService &GetGroundItems() {
            return _groundItems;
        }

        /** Alchemy sessions, table occupancy and recipe knowledge, in memory, for the `Crafting` binding. */
        KCDC::Features::Crafting::CraftingService &GetCrafting() {
            return _crafting;
        }

        /** The replicated clock and weather, for `/world` and the `World` binding. */
        KCDC::Features::World::WorldService &GetWorld() {
            return _world;
        }

        /** Server-decided status effects, for `Players` and the `Player` binding. */
        KCDC::Features::Player::Buffs &GetBuffs() {
            return _buffs;
        }

        /** Server authority over skills, stats and perks, for the `Progression` and `Player` bindings. */
        KCDC::Features::Player::Progression &GetProgression() {
            return _progression;
        }

        /** First-claim authority over doors, for the `Door` binding. The server owns their locks but has no world to look in. */
        KCDC::Features::Area::AreaService &GetAreas() {
            return _areas;
        }

        KCDC::Features::Light::LightService &GetLights() { return _lights; }

        KCDC::Features::WorldBuilder::WorldBuilderService &GetWorldResources() {
            return _worldBuilder;
        }

        KCDC::Features::Door::DoorService &GetDoors() {
            return _doors;
        }

        /** Authority over animated gates, for the `Gate` binding. */
        KCDC::Features::Door::GateService &GetGates() {
            return _gates;
        }

        /** Replicated containers and their contents, for the `Stash` binding. */
        KCDC::Features::Stash::StashService &GetStashes() {
            return _stashes;
        }

        /** Everything about players that is not about one player: deaths and revives. */
        KCDC::Features::Player::PlayerService &GetPlayers() {
            return _players;
        }

        KCDC::Features::Player::Combat &GetCombat() {
            return _combat;
        }

        /** The book of item removals asked of players' own clients, for `Players::TakeItem`. */
        KCDC::Features::Player::ItemRequests &GetItemRequests() {
            return _itemRequests;
        }

        /** The loaded navigation mesh for the `Navigation` binding and NPC routes. */
        KCDC::Features::Navigation::NavigationService &GetNavigation() {
            return _navigation;
        }

        /** What a trace meets and which entities are nearby, borrowed from a client's world, for the `World` binding. */
        KCDC::Features::World::WorldQueryService &GetWorldQueries() {
            return _worldQueries;
        }

        /** Why the configured `mod` object cannot be honoured, or empty. `main` turns it into a non-zero exit. */
        const std::string &GetConfigError() const {
            return _configError;
        }

        /** The running server, for the scripting builtins to reach back into. */
        static Server *Get();
        KCDC::Features::Inventory::InventoryService &GetInventory() {
            return _inventory;
        }

      private:
        /** Checks the `required_dlc` spellings, filling `_configError` on a bad one. The list is replicated for clients to honour; nothing here enforces it. */
        void ValidateRequiredDLC();

        KCDC::Features::Inventory::InventoryService _inventory;
        KCDC::Features::StreamedAssets::ModTableService _modTables;
        bool _defaultChatRelay = true;
        KCDC::Features::WorldBuilder::WorldBuilderService _worldBuilder;
        KCDC::Features::Light::LightService _lights;
        std::string _configError;
        KCDC::Features::Crafting::CraftingService _crafting;
        KCDC::Features::GroundItem::GroundItemService _groundItems;
        KCDC::Features::Gathering::GatheringService _gathering;
        KCDC::Features::Door::DoorService _doors;
        KCDC::Features::Area::AreaService _areas;
        KCDC::Features::Door::GateService _gates;
        KCDC::Features::CookPot::CookPotService _cookPots;
        KCDC::Features::Navigation::NavigationService _navigation;
        KCDC::Core::Presentation _presentations;
        KCDC::Features::Player::Missile _missiles;
        KCDC::Features::Player::Rock _rocks;
        KCDC::Features::Player::Combat _combat;
        KCDC::Features::Player::Buffs _buffs;
        KCDC::Features::Player::Progression _progression;
        KCDC::Features::Player::PlayerService _players;
        KCDC::Features::Player::ItemRequests _itemRequests;
        KCDC::Features::Social::EmoteService _emotes;
        KCDC::Features::Social::ScoreboardService _scoreboard;
        KCDC::Features::Stash::StashService _stashes;
        KCDC::Features::World::WorldService _world;
        KCDC::Features::World::WorldQueryService _worldQueries;
    };
} // namespace KCDC
