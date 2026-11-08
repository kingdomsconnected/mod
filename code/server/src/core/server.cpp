#include "server.h"
#include "features/npc/npc_inventory_module.h"
#include "features/patrol/patrol_routes.h"
#include "features/player/player_service.h"
#include "features/player/player_station_service.h"
#include "interest_config.h"

#include "features/animation/animation_verbs.h"
#include "features/cart/cart_service.h"
#include "features/cart/cart_verbs.h"
#include "features/dialogue/dialogue_service.h"
#include "features/dice/dice_service.h"
#include "features/dog/dog_verbs.h"
#include "features/horse/horse_navigation.h"
#include "features/horse/horse_scripting.h"
#include "features/horse/horse_verbs.h"
#include "features/marker/marker_verbs.h"
#include "features/pickpocket/pickpocket_service.h"
#include "features/player/player_scripting.h"
#include "features/prop/prop_verbs.h"
#include "features/quest/quest_verbs.h"
#include "features/siege/siege_ladder_service.h"
#include "features/siege/siege_service.h"
#include "features/siege/stone_pile_service.h"
#include "features/smart_object/smart_object_service.h"
#include "features/social/social_chat_scripting.h"
#include "features/vendor/vendor_service.h"
#include "scripting/script_context.h"
#include "scripting/wrap.h"
#include "scripting_registry.h"

#include "shared/features/dog/dog_entity.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/door/gate_entity.h"
#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/horse/horse_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/prop/prop_entity.h"
#include "shared/features/stash/stash_entity.h"
#include "shared/game/dlc_catalog.h"
#include "shared/register_entities.h"

#include <core_modules.h>
#include <integrations/server/scripting/module.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <networking/rpc/chat_message.h>
#include <scripting/node_engine.h>

namespace KCDC {
    namespace {
        constexpr const char *kLogger = "KCDCServer";

        Server *gServer = nullptr;

    } // namespace

    Server *Server::Get() {
        return gServer;
    }

    void Server::PostScriptInit() {
        // Before the resources start, so no start goes unseen. A resource's classes join once it has
        // started, after its own start handlers; one that depends on it starts later and sees them.
        if (auto *scripting = GetScriptingModule(); scripting != nullptr && scripting->GetResourceManager() != nullptr) {
            _modTables.Install(*scripting->GetResourceManager());
        }
    }

    void Server::PostInit() {
        gServer = this;

        // Both peers must register the same types before any replica arrives.
        Shared::Entities::RegisterEntities();

        // Every resource runs in a runtime of its own: its handles cross to
        // other resources as themselves, and the bindings' classes go with
        // the runtime.
        Core::RegisterTransfers();
        if (auto *scripting = GetScriptingModule(); scripting != nullptr && scripting->GetEngine() != nullptr) {
            scripting->GetEngine()->AddRuntimeDisposingListener([](v8::Isolate *isolate) { Core::UnregisterGlobals(isolate); });
        }

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            ConfigureInterest(*replication);
        }

        ValidateRequiredDLC();
        _inventory.Install();
        _crafting.Install();

        KCDC::Features::Horse::HorseService::Install();
        KCDC::Features::Cart::CartService::Get().Install();
        KCDC::Features::Player::PlayerService::Install();
        KCDC::Features::Animation::Animations::Install();
        _groundItems.Install();
        _gathering.Install(GetModConfig().Get<std::string>("level"));
        _doors.Install(GetModConfig().Get<std::string>("level"));
        KCDC::Features::SmartObject::SmartObjectService::Get().Install(GetModConfig().Get<std::string>("level"));
        _gates.Install(GetModConfig().Get<std::string>("level"));
        _cookPots.Install(GetModConfig().Get<std::string>("level"));
        _navigation.Install(GetModConfig().Get<std::string>("level"), GetModConfig().Get<std::string>("navmesh"), _doors);
        _areas.Install(GetModConfig().Get<std::string>("level"));
        _presentations.Install(_doors, _combat);
        _stashes.Install(GetModConfig().Get<std::string>("level"));
        _missiles.Install();
        _rocks.Install();
        _combat.Install(_missiles);
        _emotes.Install();
        _scoreboard.Install(GetModConfig().Get<bool>("scoreboard"));
        _buffs.Install();
        _progression.Install();
        _itemRequests.Install();
        _world.Install();
        KCDC::Features::Marker::Markers::Install();
        KCDC::Features::Npc::Npcs::Install();
        KCDC::Features::Npc::NpcInventoryModule::Install();
        KCDC::Features::Dialogue::DialogueService::Get().Install();
        KCDC::Features::Vendor::VendorService::Get().Install();
        KCDC::Features::Pickpocket::PickpocketService::Get().Install();
        KCDC::Features::Dice::DiceService::Get().Install();
        KCDC::Features::Player::StationService::Get().Install();
        KCDC::Features::Siege::SiegeService::Get().Install();
        KCDC::Features::Siege::SiegeLadderService::Get().Install();
        KCDC::Features::Siege::StonePileService::Get().Install();
        KCDC::Features::Quest::Quests::Install();
        _worldQueries.Install(GetModConfig().Get<std::string>("level"));
    }

    void Server::LoadWorldResources() {
        if (!_configError.empty() || _worldBuilder.IsReady()) {
            return;
        }
        if (!_worldBuilder.Load(GetModConfig().Get<nlohmann::json>("world_resources"), GetModConfig().Get<std::string>("level"), _configError)) {
            Framework::Logging::GetLogger(kLogger)->critical("World resources: {}", _configError);
            return;
        }
        EmitReservedEvent("worldResourcesReady", [](v8::Isolate *) { return std::vector<v8::Local<v8::Value>> {}; });
    }

    void Server::PreShutdown() {
        _worldBuilder.Reset();
        Features::Patrol::PatrolRoutes::Get().Clear();
        _gathering.Reset();
        _stashes.Reset();
        _crafting.Reset();
        _inventory.Reset();
        KCDC::Features::Npc::NpcInventoryModule::Get().Reset();
        // The host runtime is torn down without telling its listeners.
        Scripting::PlayerHitHandles::Get().Clear();
    }

    void Server::PostUpdate() {
        _modTables.VerifyShipped(GetAssetPaks());
        _buffs.Update();
        _players.UpdateStats();
        _inventory.Update();
        _crafting.Update();
        _world.Update();
        _groundItems.Update();
        _gathering.Update();
        _presentations.Update();
        _doors.Update();
        KCDC::Features::SmartObject::SmartObjectService::Get().Update();
        _gates.Update();
        _cookPots.Update();
        _areas.Update();
        _stashes.Update();
        _missiles.Update();
        _rocks.Update();
        _combat.Update();
        _worldQueries.Update();
        KCDC::Features::Prop::Props::Update();
        _itemRequests.Update();
        KCDC::Features::Pickpocket::PickpocketService::Get().Update();
        KCDC::Features::Dice::DiceService::Get().Update();
        KCDC::Features::Player::StationService::Get().Update();
        KCDC::Features::Siege::SiegeService::Get().Update();
        KCDC::Features::Siege::SiegeLadderService::Get().Update();
        KCDC::Features::Npc::Npcs::Update();
        KCDC::Features::Horse::HorseNavigation::Get().Update();
        KCDC::Features::Npc::NpcInventoryModule::Get().Update();
    }

    // Resolve the rider from the sending connection, not the message.
    void Server::OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) {
        auto *replication = Framework::CoreModules::GetReplication();

        auto *human = replication->CreateEntity<Shared::Entities::HumanEntity>();
        if (!human) {
            Framework::Logging::GetLogger(kLogger)->error("Could not create a human entity for '{}'", data.nickname);
            return;
        }

        human->ownerGUID       = data.guid;
        human->nickname        = data.nickname;
        human->playerIndex     = data.playerIndex;
        human->streaming.range = Shared::Entities::HumanEntity::kStreamRange;
        // Visible to everyone until the owner reports where it is; the body
        // promotes itself to range streaming from HumanEntity::OnDeserialized.
        human->streaming.alwaysVisible = true;
        human->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        replication->SetViewer(data.guid, human);

        Framework::Logging::GetLogger(kLogger)->info("Player '{}' joined as human entity {} (slot {})", human->nickname, human->GetNetworkID(), human->playerIndex);

        // Hands the arriving client the current buff claim before anything can
        // put a buff on it, so its refusal gate is armed rather than learning
        // the policy a frame late.
        _buffs.OnPlayerConnect(data.guid);
        _progression.OnPlayerConnect(data.guid);
        _scoreboard.OnRosterChanged();

        // Before the event, so a script can restore a saved inventory from it.
        _inventory.OnPlayerConnect(data.guid, human->GetNetworkID());
        _crafting.OnPlayerConnect(data.guid);

        Scripting::Events::PlayerConnected(human->GetNetworkID());
    }

    void Server::OnPlayerReady(MafiaNet::PeerGuid guid) {
        auto *human = Framework::CoreModules::GetReplication()->GetViewerAs<Shared::Entities::HumanEntity>(guid);
        if (human == nullptr) {
            return;
        }
        _inventory.OnPlayerReady(guid);
        _players.OnPlayerSpawned(guid);
        Scripting::Events::PlayerSpawned(human->GetNetworkID());
    }

    void Server::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        // Before anything else, so the refunds of an unfinished brew are in
        // the inventory `playerDisconnect` saves.
        _crafting.OnPlayerDisconnect(guid);
        KCDC::Features::Cart::CartService::Get().Forget(guid);
        _players.OnPlayerDisconnect(guid);
        // The replication manager destroys the viewer right after this
        // returns, which is what broadcasts the despawn.
        auto *replication = Framework::CoreModules::GetReplication();
        auto *human       = replication->GetViewerAs<Shared::Entities::HumanEntity>(guid);
        if (human == nullptr) {
            _inventory.OnPlayerDisconnect(guid);
            return;
        }
        KCDC::Features::Player::StationService::Get().Forget(human->GetNetworkID());

        _groundItems.OnPlayerDisconnect(guid);
        _gathering.OnPlayerDisconnect(guid);
        _cookPots.OnPlayerDisconnect(guid);
        _presentations.OnPlayerDisconnect(guid, human->GetNetworkID());
        _doors.OnPlayerDisconnect(guid);
        _stashes.OnPlayerDisconnect(guid);
        KCDC::Features::Npc::NpcInventoryModule::Get().Forget(human->GetNetworkID());
        _missiles.OnPlayerDisconnect(guid);
        _combat.OnPlayerDisconnect(human->GetNetworkID());
        _worldQueries.OnPlayerDisconnect(guid);
        _itemRequests.OnPlayerDisconnect(guid);
        _emotes.OnPlayerDisconnect(guid);
        _scoreboard.OnPlayerDisconnect(guid);
        _buffs.OnPlayerDisconnect(guid, human->GetNetworkID());
        _progression.OnPlayerDisconnect(guid, human->GetNetworkID());
        KCDC::Features::Siege::SiegeService::Get().OnPlayerDisconnect(human->GetNetworkID());
        KCDC::Features::Siege::StonePileService::Get().OnPlayerDisconnect(human->GetNetworkID());

        // A conversation belongs to one player, so it leaves with them. No
        // close goes out: the only peer it would reach is the one that left.
        KCDC::Features::Dialogue::DialogueService::Get().Forget(human->GetNetworkID());

        // As is a trading session, for the same reason. A deal still settling
        // is not cut short: its steps resolve the player themselves, and a
        // player who is gone settles as one who ran out.
        KCDC::Features::Vendor::VendorService::Get().Forget(human->GetNetworkID());

        // An attempt either side of which has gone is over. A theft already
        // settling is not: its takes resolve the players themselves, and what
        // a departed thief was owed goes back to the victim.
        KCDC::Features::Pickpocket::PickpocketService::Get().Forget(human->GetNetworkID());

        // A player who leaves a dice match loses it; the other's table closes.
        KCDC::Features::Dice::DiceService::Get().Forget(human->GetNetworkID());

        // Nor does a dropped passenger climb down, and a cart whose driver has
        // gone would stay pinned to a connection that no longer exists.
        KCDC::Features::Cart::Carts::Unseat(human->GetNetworkID());

        // A dropped rider never sends the dismount, and a horse still owned by
        // a connection that is gone is a horse nobody can ever move again.
        if (auto *horse = KCDC::Features::Horse::Horses::UnseatPlayer(human->GetNetworkID())) {
            Scripting::Events::HorseDismounted(horse->GetNetworkID(), human->GetNetworkID());
        }

        // A dog goes with the player it belonged to. The framework would hand
        // it back to the server instead, which is the wrong answer for this
        // one type: a masterless dog is Forsaken in the game's own terms, and
        // the world is authored to notice one. Horses go the same way, so a
        // player's animals do not outlive them in a world they have left --
        // unless a script cleared `destroyWithOwner` to keep them.
        KCDC::Features::Dog::Dogs::OwnerLeft(human->GetNetworkID());
        KCDC::Features::Horse::Horses::OwnerLeft(human->GetNetworkID());

        Framework::Logging::GetLogger(kLogger)->info("Player '{}' left (human entity {})", human->nickname, human->GetNetworkID());

        // Every inventory change is raised before the player is gone, and the
        // inventory is still readable from playerDisconnect, so a script can
        // save it there.
        _inventory.DispatchChanges();
        Scripting::Events::PlayerDisconnected(human->GetNetworkID());
        _inventory.OnPlayerDisconnect(guid);
        _crafting.OnPlayerLeft(human->GetNetworkID());
    }

    void Server::OnChatMessage(uint64_t senderNetworkId, const std::string &text) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *sender      = replication != nullptr ? replication->GetEntity<Shared::Entities::HumanEntity>(senderNetworkId) : nullptr;

        if (_defaultChatRelay) {
            // The sender is included: the client shows nothing of its own
            // when it sends, so this is what puts the line in its own log.
            if (auto *net = Framework::CoreModules::GetNetworkPeer()) {
                Framework::Networking::RPC::ChatMessage payload;
                payload.text   = text;
                payload.author = sender != nullptr ? sender->nickname : "Player";

                // Names the body that said it, so a client can speak the line
                // over it. Left at zero when no body did.
                payload.senderNetworkId = sender != nullptr ? sender->GetNetworkID() : 0;

                net->BroadcastRPC(payload);
            }
        }

        Framework::Logging::GetLogger(kLogger)->info("[chat] {}: {}", sender != nullptr ? sender->nickname : "?", text);

        Scripting::Events::PlayerChat(senderNetworkId, text);
    }

    void Server::OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command, const std::vector<std::string> &args) {
        // Every `/` command lives in a resource now -- `resources/kcdc-gamemode`
        // is the one that ships -- so the server claims none itself, and a `/`
        // line is never relayed to anyone else either way.
        Scripting::Events::PlayerCommand(senderNetworkId, command, args);
    }

    void Server::ModuleRegister(Framework::Scripting::Engine *engine) {
        auto *nodeEngine = dynamic_cast<Framework::Scripting::NodeEngine *>(engine);
        if (nodeEngine == nullptr) {
            return;
        }

        v8::Isolate *isolate = nodeEngine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = nodeEngine->GetContext();
        v8::Context::Scope contextScope(context);

        Core::RegisterScripting(isolate, context->Global());
    }

    // Wrap players consistently with the project-specific scripting handle.
    v8::Local<v8::Value> Server::WrapScriptPlayer(v8::Isolate *isolate, uint64_t networkId) {
        return Scripting::WrapPlayer(isolate, networkId);
    }

    void Server::ValidateRequiredDLC() {
        for (const std::string &name : GetModConfig().Get<std::vector<std::string>>("required_dlc")) {
            const Shared::Game::DLCEntry *dlc = Shared::Game::FindDLCByName(name);
            if (dlc == nullptr) {
                _configError = "'" + name + "' is not a DLC this build knows. Use one of the game's own names, such as ForgeTycoon or MysteriaEcclesiae.";
            }
            else if (!dlc->ownable) {
                _configError = "'" + name + "' is a development row with no store product behind it, so no account can hold it and no client could ever join.";
            }
            else {
                continue;
            }
            // PostInit runs after the port is bound, so main does the exiting.
            Framework::Logging::GetLogger(kLogger)->critical("server.json: mod.required_dlc: {}", _configError);
            return;
        }
    }

} // namespace KCDC
