#include "core/server.h"
#include "shared/features/inventory/inventory_gameplay_message.h"
#include "shared/features/inventory/inventory_message.h"
#include "shared/features/world/world_entity.h"
#include "shared/game/buff_catalog.h"
#include <chrono>
#include <core_modules.h>
#include <filesystem>
#include <fstream>
#include <integrations/server/scripting/module.h>
#include <iostream>
#include <limits>
#include <networking/network_client.h>
#include <networking/network_server.h>
#include <networking/replication/replication_manager.h>
#include <optional>
#include <scripting/engine.h>
#include <stdexcept>
#include <thread>

namespace {
    void Check(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    class PotClient final {
      public:
        Framework::Networking::NetworkClient peer {"127.0.0.1"};
        KCDC::Shared::Entities::HumanEntity *player = nullptr;
        KCDC::Shared::RPC::CookPotContext context;
        std::vector<KCDC::Shared::RPC::CookPotReply> replies;
        std::vector<KCDC::Shared::RPC::ApplyBuff> effects;
        std::vector<nlohmann::json> foodReplies;
        std::uint64_t sequence = 0;
        std::optional<std::uint64_t> definitionRevision;
        std::optional<std::uint32_t> inventoryWorld;
        std::uint64_t inventoryRevision = 0;

        void Init() {
            Check(static_cast<bool>(peer.Init()), "client starts");
            peer.SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);
            peer.RegisterRPC<KCDC::Shared::RPC::CookPotContext>([this](const auto &message, MafiaNet::Packet *) {
                context = message;
            });
            peer.RegisterRPC<KCDC::Shared::RPC::CookPotReply>([this](const auto &message, MafiaNet::Packet *) {
                replies.push_back(message);
            });
            peer.RegisterRPC<KCDC::Shared::RPC::ApplyBuff>([this](const auto &message, MafiaNet::Packet *) {
                effects.push_back(message);
            });
            peer.RegisterRPC<KCDC::Shared::RPC::InventoryConsumeResult>([this](const auto &message, MafiaNet::Packet *) {
                foodReplies.push_back(nlohmann::json::parse(message.body));
            });
            peer.RegisterRPC<KCDC::Shared::RPC::CustomItemDefinitions>([this](const auto &message, MafiaNet::Packet *) {
                Check(message.valid, "valid custom item definition envelope");
                const auto body = nlohmann::json::parse(message.body);
                if (body.value("begin", false)) {
                    definitionRevision.reset();
                }
                if (body.value("end", false)) {
                    definitionRevision = body.at("revision").template get<std::uint64_t>();
                }
            });
            peer.RegisterRPC<KCDC::Shared::RPC::InventorySync>([this](const auto &message, MafiaNet::Packet *packet) {
                const auto body = nlohmann::json::parse(message.body);
                Check(definitionRevision.has_value(), "custom item definitions arrive before inventory projection");
                inventoryWorld    = body.at("world").get<std::uint32_t>();
                inventoryRevision = body.at("revision").get<std::uint64_t>();
                KCDC::Shared::RPC::InventoryApplied applied;
                applied.body = nlohmann::json({{"revision", body.at("revision")}, {"applied", true}, {"definitionRevision", *definitionRevision}}).dump();
                peer.SendRPC(applied, packet->guid);
            });
        }
    };

    class PotFixture final {
      public:
        KCDC::Server server;
        PotClient first;
        PotClient second;
        std::uint64_t guid = 0;
        glm::vec3 position;
        std::uint64_t scriptSequence = 0;

        PotFixture() {
            std::filesystem::create_directories("resources/pot-tests");
            std::ofstream("resources/pot-tests/package.json") << R"({"name":"pot-tests","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})";
            std::ofstream("resources/pot-tests/server.js") << "// Pot API regression resource\n";
            std::ofstream("server.json") << R"({"mod":{"level":"klaster","required_dlc":[],"npcEnabled":false,"scoreboard":false,"world_resources":[]}})";
            Framework::Integrations::Server::InstanceOptions options {};
            options.modSlug          = "pot-tests";
            options.modName          = "Pot tests";
            options.modVersion       = "2.0.0";
            options.gameName         = "pot-tests";
            options.gameVersion      = "1";
            options.bindHost         = "127.0.0.1";
            options.bindPort         = 28979;
            options.maxPlayers       = 4;
            options.bindPublicServer = false;
            options.verifyBuildToken = false;
            options.enableSignals    = false;
            options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "klaster"}, {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}, {"npcEnabled", Framework::Utils::ConfigFieldType::Bool, false},
                 {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false}, {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}};
            char executable[]        = "pot-tests";
            char *argv[]             = {executable};
            options.argc             = 1;
            options.argv             = argv;
            const auto started       = server.Init(options);
            if (!started) {
                throw std::runtime_error(started.GetError().message);
            }
            Network().SetOnSessionRequestCallback([this](MafiaNet::RakNetGUID id, const auto &) {
                Network().AcceptSession(id);
            });
            Network().SetOnClientAuthenticatedCallback([](MafiaNet::RakNetGUID) {});
            Check(Framework::CoreModules::GetReplication()->CreateEntity<KCDC::Shared::Entities::WorldEntity>() != nullptr, "world created");
            for (const auto &placement : KCDC::Shared::Game::g_CookPotPlacements) {
                if (std::string_view(placement.level) == "klaster") {
                    guid     = placement.guid;
                    position = placement.position;
                    break;
                }
            }
            Check(guid != 0, "test pot exists");
            first.Init();
            second.Init();
            Check(static_cast<bool>(first.peer.Connect("127.0.0.1", 28979)), "first connects");
            Check(static_cast<bool>(second.peer.Connect("127.0.0.1", 28979)), "second connects");
            Wait([&] {
                return first.peer.GetConnectionState() == Framework::Networking::PeerState::CONNECTED && second.peer.GetConnectionState() == Framework::Networking::PeerState::CONNECTED;
            });
            AddPlayer(first);
            AddPlayer(second);
            Ready(first);
            Ready(second);
            Wait([&] {
                return first.context.revision && second.context.revision;
            });
        }

        ~PotFixture() {
            first.peer.Shutdown();
            second.peer.Shutdown();
            server.Shutdown();
        }

        Framework::Networking::NetworkServer &Network() {
            return *static_cast<Framework::Networking::NetworkServer *>(Framework::CoreModules::GetNetworkPeer());
        }

        void Pump() {
            Network().Update();
            server.GetInventory().Update();
            server.GetCookPots().Update();
            first.peer.Update();
            second.peer.Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        template <typename Predicate>
        void Wait(Predicate predicate) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!predicate()) {
                Check(std::chrono::steady_clock::now() < deadline, "pot test timed out");
                Pump();
            }
        }

        void Drain() {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(80);
            while (std::chrono::steady_clock::now() < deadline) {
                Pump();
            }
        }

        void ScriptCheck() {
            std::ofstream("resources/pot-tests/check.js") << R"(
                (() => {
                    const pot = CookPot.all()[0];
                    if (!pot || !Object.isFrozen(pot) || !Object.isFrozen(pot.position)) throw Error("frozen pot snapshot");
                    if (!CookPot.setFood(pot.guid, "lentil") || CookPot.get(pot.guid).nutrition !== 10) throw Error("meal binding");
                    if (!CookPot.setFood(pot.guid, "soup") || CookPot.get(pot.guid).food !== "soup") throw Error("food binding");
                    if (!CookPot.poisons().includes("42e54d97-6e63-4e50-a09d-325ef4dd2286")) throw Error("poison catalog binding");
                    let threw = false;
                    try { CookPot.setFood(pot.guid, "invalid"); } catch { threw = true; }
                    if (!threw) throw Error("invalid food must throw");
                    threw = false;
                    try { CookPot.poison({}, pot.guid, "1"); } catch { threw = true; }
                    if (!threw) throw Error("invalid player must throw");
                    const player = Player.getById()" << first.player->GetNetworkID()
                                                          << R"();
                    if (!player) throw Error("player handle lookup");
                    const result = CookPot.refillFromInventory(player, pot.guid, { food: "soup", portions: 0, ingredients: [] });
                    if (result.ok || !Object.isFrozen(result)) throw Error("invalid recipe binding");
                    if (CookPot.poison(player, pot.guid, "absent").ok) throw Error("unknown poison row accepted");
                })();
            )";
            auto *engine = server.GetScriptingModule()->GetEngine();
            if (!engine->ExecuteResourceFile("pot-tests", "resources/pot-tests/check.js")) {
                throw std::runtime_error(engine->GetLastError());
            }
        }

        void Script(const std::string &source) {
            const auto path = "resources/pot-tests/consumption-" + std::to_string(++scriptSequence) + ".js";
            std::ofstream(path) << source;
            auto *engine = server.GetScriptingModule()->GetEngine();
            if (!engine->ExecuteResourceFile("pot-tests", path)) {
                throw std::runtime_error(engine->GetLastError());
            }
        }

        nlohmann::json UseFood(PotClient &client, const std::string &row, bool stale = false) {
            Ready(client);
            const auto *inventory = server.GetInventory().Find(client.player->GetNetworkID());
            nlohmann::json body {{"kind", "foodRequest"}, {"request", std::to_string(++client.sequence)}, {"rowId", row}, {"revision", inventory->Revision() + (stale ? 1 : 0)}, {"incarnation", client.player->soul.incarnation}, {"world", client.inventoryWorld.value()}};
            const auto before = client.foodReplies.size();
            SendFood(client, body);
            Wait([&] {
                return client.foodReplies.size() > before;
            });
            return body;
        }

        void SendFood(PotClient &client, const nlohmann::json &body) {
            KCDC::Shared::RPC::InventoryConsume request;
            request.body = body.dump();
            client.peer.SendRPC(request, Network().GetPeer()->GetMyGUID());
            Drain();
        }

        void Ready(PotClient &client) {
            Wait([&] {
                return server.GetInventory().IsReady(client.player->GetNetworkID());
            });
        }

        void AddPlayer(PotClient &client) {
            auto *replication = Framework::CoreModules::GetReplication();
            client.player     = replication->CreateEntity<KCDC::Shared::Entities::HumanEntity>();
            Check(client.player != nullptr, "player exists");
            client.player->streaming.visible = false;
            client.player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
            client.player->position                                                            = position;
            client.player->ownerGUID                                                           = MafiaNet::ToPeerGuid(client.peer.GetPeer()->GetMyGUID());
            client.player->soul.valid                                                          = true;
            client.player->soul.incarnation                                                    = 1;
            client.player->soul.resources[KCDC::Shared::Game::HumanSoulState::kHealth]         = {100.0F, 100.0F};
            client.player->soul.conditions[KCDC::Shared::Game::HumanSoulState::kConsciousness] = 1.0F;
            replication->SetViewer(client.player->ownerGUID, client.player);
            server.GetInventory().OnPlayerConnect(client.player->ownerGUID, client.player->GetNetworkID());
            server.GetInventory().OnPlayerReady(client.player->ownerGUID);
        }

        std::string Give(PotClient &client, const char *item, unsigned amount, double health = 1.0) {
            const auto result = server.GetInventory().ScriptAdd(client.player->GetNetworkID(), {{"item", item}, {"amount", amount}, {"metadata", {{"health", health}}}});
            Check(result.value("ok", false), "ingredient grant succeeds");
            Ready(client);
            return result.at("items").at(0).at("id").get<std::string>();
        }

        KCDC::Shared::RPC::CookPotRequest Reserve(PotClient &client) {
            KCDC::Shared::RPC::CookPotRequest request;
            request.sequence        = ++client.sequence;
            request.guid            = guid;
            request.world           = client.context.world;
            request.incarnation     = client.context.incarnation;
            request.contextRevision = client.context.revision;
            const auto before       = client.replies.size();
            client.peer.SendRPC(request, Network().GetPeer()->GetMyGUID());
            Wait([&] {
                return client.replies.size() > before;
            });
            Check(client.replies.back().valid, "valid meal approval");
            return request;
        }

        void Complete(PotClient &client, KCDC::Shared::RPC::CookPotRequest request, bool eaten) {
            request.completion = true;
            request.eaten      = eaten;
            client.peer.SendRPC(request, Network().GetPeer()->GetMyGUID());
            Drain();
        }
    };
} // namespace

int RunCookPotRpcSuite() {
    try {
        PotFixture fixture;
        auto &pots      = fixture.server.GetCookPots();
        auto &client    = fixture.first;
        auto *inventory = fixture.server.GetInventory().Find(client.player->GetNetworkID());
        const auto guid = fixture.guid;
        fixture.ScriptCheck();
        Check(pots.SetState(guid, 0, KCDC::Shared::CookPot::State::Empty), "empty the pot");
        const nlohmann::json recipe = {{"food", "soup"}, {"portions", 4}, {"ingredients", {{{"item", "breadroll"}, {"amount", 3}}, {{"item", "carrotCooked"}, {"amount", 1}}}}};
        fixture.Give(client, "breadroll", 2);
        fixture.Give(client, "breadroll", 1, 0.8);
        auto revision = inventory->Revision();
        Check(pots.RefillFromInventory(client.player, guid, recipe).at("code") == "missing_ingredients" && inventory->Revision() == revision, "missing second ingredient spends nothing");
        fixture.Give(client, "carrotCooked", 1, 0.0);
        revision = inventory->Revision();
        Check(pots.RefillFromInventory(client.player, guid, recipe).at("code") == "missing_ingredients" && inventory->Revision() == revision, "spoiled ingredient is not charged");
        fixture.Give(client, "carrotCooked", 1);
        revision            = inventory->Revision();
        auto invalid        = recipe;
        invalid["portions"] = 1.5;
        Check(!pots.RefillFromInventory(client.player, guid, invalid).at("ok").get<bool>() && inventory->Revision() == revision, "fractional portions rejected before debit");
        client.player->position.x = std::numeric_limits<float>::quiet_NaN();
        Check(pots.RefillFromInventory(client.player, guid, recipe).at("code") == "too_far", "nonfinite position refused");
        client.player->position = fixture.position;
        Check(pots.RefillFromInventory(client.player, guid, recipe).at("ok"), "split inventory stacks fund one refill");
        Check(inventory->Rows().size() == 1 && inventory->Rows().begin()->second.metadata.value("health", 1.0) == 0.0 && inventory->Revision() == revision + 1, "all ingredients spent in one revision");
        Check(pots.Snapshot(guid, 0).at("food") == "soup" && pots.Snapshot(guid, 0).at("nutrition") == 15, "new meal and nutrition recorded");
        fixture.Ready(client);
        Check(pots.RefillFromInventory(client.player, guid, recipe).at("code") == "not_empty", "cannot overwrite food through player refill");
        const auto nonPoisonRow = inventory->Rows().begin()->first;
        const auto poisonRow    = fixture.Give(client, "banePoison", 2);
        revision                = inventory->Revision();
        Check(pots.Poison(client.player, guid, "absent").at("code") == "unknown_item", "absent row refused on a clean pot");
        Check(pots.Poison(client.player, guid, nonPoisonRow).at("code") == "not_poison", "food row refused on a clean pot");
        Check(inventory->Revision() == revision, "invalid poison rows spend nothing on a clean pot");
        Check(pots.Poison(client.player, guid, poisonRow).at("ok"), "one dose poisons the pot");
        Check(inventory->Rows().at(poisonRow).amount == 1, "exactly one poison consumed");
        fixture.Ready(client);
        revision = inventory->Revision();
        Check(pots.Poison(client.player, guid, "absent").at("code") == "unknown_item", "absent row cannot detect a poisoned pot");
        Check(pots.Poison(client.player, guid, nonPoisonRow).at("code") == "not_poison", "food row cannot detect a poisoned pot");
        Check(inventory->Revision() == revision, "invalid poison rows spend nothing on a poisoned pot");
        Check(pots.Poison(client.player, guid, poisonRow).at("code") == "already_poisoned" && inventory->Revision() == revision, "second dose refused without loss");
        Check(pots.SetState(guid, 0, KCDC::Shared::CookPot::State::Half), "half filled");
        Check(pots.Snapshot(guid, 0).at("poison") == "58138b68-0c86-4d5e-8823-417106d08d3c", "fill-level change preserves ingested poison");

        const auto meal = fixture.Reserve(client);
        Check(client.replies.back().granted && client.replies.back().food == KCDC::Shared::Game::CookPotFood::Soup, "meal pins food type");
        pots.SetFood(guid, 0, KCDC::Shared::Game::CookPotFood::Goulash);
        pots.Refill(guid, 0, 1);
        Check(pots.Snapshot(guid, 0).at("poison").is_null(), "fresh refill clears poison");
        fixture.Complete(client, meal, true);
        fixture.Wait([&] {
            return client.effects.size() == 1;
        });
        Check(client.effects[0].buff == KCDC::Shared::Game::BuffIdFromGuid("58138b68-0c86-4d5e-8823-417106d08d3c"), "reserved meal delivers its original ingestion effect after refill");
        Check(client.effects[0].incarnation == meal.incarnation && client.effects[0].AppliesTo(meal.incarnation), "poison delivery retains the consuming body");
        Check(!client.effects[0].AppliesTo(0) && !client.effects[0].AppliesTo(meal.incarnation + 1), "delayed poison cannot affect an absent or replacement body");
        fixture.Complete(client, meal, true);
        Check(client.effects.size() == 1, "duplicate receipt cannot apply twice");

        const auto refused = fixture.Reserve(fixture.second);
        Check(fixture.second.replies.back().granted && pots.Snapshot(guid, 0).at("portions") == 0, "last portion reserved");
        Check(pots.SetFood(guid, 0, KCDC::Shared::Game::CookPotFood::Soup), "change the future recipe without replacing stock");
        fixture.Complete(fixture.second, refused, false);
        Check(pots.Snapshot(guid, 0).at("portions") == 1 && fixture.second.effects.empty(), "native refusal after a food change refunds without effect");
        fixture.Complete(fixture.second, refused, false);
        Check(pots.Snapshot(guid, 0).at("portions") == 1, "refund happens once");
        Check(pots.Snapshot(guid, 0).at("food") == "soup", "refund preserves the new recipe");

        pots.Refill(guid, 7, 4);
        pots.SetFood(guid, 7, KCDC::Shared::Game::CookPotFood::Lentil);
        Check(pots.Snapshot(guid, 0).at("food") == "soup" && pots.Snapshot(guid, 7).at("food") == "lentil", "worlds have independent food");
        pots.SetEnabled(guid, 0, false);
        revision = inventory->Revision();
        Check(pots.Poison(client.player, guid, poisonRow).at("code") == "disabled" && inventory->Revision() == revision, "disabled pot cannot consume poison");
        pots.SetEnabled(guid, 0, true);
        client.player->position.x += 10;
        Check(pots.Poison(client.player, guid, poisonRow).at("code") == "too_far", "distant poison rejected");
        client.player->position = fixture.position;
        revision                = inventory->Revision();
        client.player->SetVirtualWorld(7);
        const auto oldContext = client.context.revision;
        fixture.Wait([&] {
            return client.context.revision != oldContext && client.inventoryWorld == 7;
        });
        Check(inventory->Revision() == revision && client.inventoryRevision == revision, "world context publishes without an inventory row revision");
        Check(pots.Poison(client.player, guid, poisonRow).at("ok"), "poison affects current world");
        Check(pots.Snapshot(guid, 0).at("poison").is_null() && !pots.Snapshot(guid, 7).at("poison").is_null(), "poison isolated between worlds");
        fixture.Complete(client, meal, true);
        Check(client.effects.size() == 1, "old-world receipt refused");

        fixture.Script(R"(
            globalThis.consumptionMode = "allow";
            globalThis.consumedCount = 0;
            globalThis.consumingCount = 0;
            globalThis.poisonCount = 0;

            Events.on("playerConsuming", (player, meal) => {
                ++globalThis.consumingCount;
                try { meal.nativeEffects = false; meal.metadata.health = 0; } catch {}
                if (globalThis.consumptionMode === "restore") Inventory.set(player, {items: []});
                return globalThis.consumptionMode !== "deny";
            });
            Events.on("playerConsumptionEffects", (player) => {
                if (globalThis.consumptionMode === "restoreEffects") Inventory.set(player, {items: []});
                return globalThis.consumptionMode !== "custom";
            });
            Events.on("playerPoisonAbsorbing", () => { ++globalThis.poisonCount; return false; });
            Events.on("playerConsumed", (player, meal) => { ++globalThis.consumedCount; globalThis.lastConsumption = meal; });
        )");
        const auto foodRow = fixture.Give(client, "potion_marigold", 4);
        fixture.Script("globalThis.consumptionMode = 'deny';");
        fixture.UseFood(client, foodRow);
        Check(!client.foodReplies.back().at("ok").get<bool>() && inventory->Rows().at(foodRow).amount == 4, "veto preserves the potion");
        fixture.Script("globalThis.consumptionMode = 'allow';");
        auto staleWorld = fixture.UseFood(client, foodRow, true);
        Check(!client.foodReplies.back().at("ok").get<bool>() && inventory->Rows().at(foodRow).amount == 4, "stale request spends nothing");
        staleWorld["request"]  = std::to_string(++client.sequence);
        staleWorld["revision"] = inventory->Revision();
        staleWorld["world"]    = MafiaNet::VIRTUAL_WORLD_GLOBAL;
        fixture.SendFood(client, staleWorld);
        Check(client.foodReplies.back().at("code") == "invalidContext" && inventory->Rows().at(foodRow).amount == 4, "old-world inventory request spends nothing");
        Check(client.inventoryWorld == 7, "numbered inventory world received over the wire");
        const auto use = fixture.UseFood(client, foodRow);
        Check(client.foodReplies.back().at("ok") && client.foodReplies.back().at("nativeEffects") && inventory->Rows().at(foodRow).amount == 3, "approval reserves exactly one potion");
        fixture.SendFood(client, use);
        Check(inventory->Rows().at(foodRow).amount == 3, "duplicate request never debits twice");
        fixture.SendFood(fixture.second, {{"kind", "foodReceipt"}, {"request", use.at("request")}, {"eaten", true}});
        fixture.Script("if (globalThis.consumedCount !== 0) throw Error('foreign receipt');");
        fixture.SendFood(client, {{"kind", "foodReceipt"}, {"request", use.at("request")}, {"eaten", false}});
        fixture.SendFood(client, {{"kind", "foodReceipt"}, {"request", use.at("request")}, {"eaten", false}});
        Check(inventory->Rows().at(foodRow).amount == 4, "native refusal refunds once");
        fixture.Script("globalThis.consumptionMode = 'custom';");
        const auto custom = fixture.UseFood(client, foodRow);
        Check(client.foodReplies.back().at("ok") && !client.foodReplies.back().at("nativeEffects").get<bool>(), "custom effects suppress native application before drinking");
        fixture.SendFood(client, {{"kind", "foodReceipt"}, {"request", custom.at("request")}, {"eaten", true}});
        fixture.SendFood(client, {{"kind", "foodReceipt"}, {"request", custom.at("request")}, {"eaten", true}});
        fixture.Script(
            "if (globalThis.consumedCount !== 1 || globalThis.lastConsumption.nativeEffects || globalThis.lastConsumption.metadata.health !== 1 || globalThis.lastConsumption.kind !== 'potion' || globalThis.lastConsumption.baseEffects.health !== 15) throw Error('custom consumption snapshot');");
        fixture.Script("globalThis.consumptionMode = 'allow';");
        const auto abandoned = fixture.UseFood(client, foodRow);
        ++client.player->soul.incarnation;
        fixture.SendFood(client, {{"kind", "foodReceipt"}, {"request", abandoned.at("request")}, {"eaten", true}});
        fixture.Script("if (globalThis.consumedCount !== 1) throw Error('replacement body receipt');");
        fixture.Wait([&] {
            return client.context.incarnation == client.player->soul.incarnation;
        });
        const auto effectsBefore = client.effects.size();
        const auto poisonMeal    = fixture.Reserve(client);
        Check(client.replies.back().granted, "poison meal allowed");
        fixture.Complete(client, poisonMeal, true);
        Check(client.effects.size() == effectsBefore, "poison veto prevents effect delivery");
        fixture.Script("if (globalThis.poisonCount !== 1 || globalThis.consumedCount !== 2) throw Error('pot consumption events');");
        client.player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_DEFAULT);
        fixture.Wait([&] {
            return client.inventoryWorld == MafiaNet::VIRTUAL_WORLD_DEFAULT;
        });
        const auto defaultUse = fixture.UseFood(client, foodRow);
        Check(client.foodReplies.back().at("ok"), "default-world inventory request accepted");
        fixture.SendFood(client, {{"kind", "foodReceipt"}, {"request", defaultUse.at("request")}, {"eaten", false}});
        client.player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        fixture.Wait([&] {
            return client.inventoryWorld == MafiaNet::VIRTUAL_WORLD_GLOBAL;
        });
        const auto globalUse = fixture.UseFood(client, foodRow);
        Check(client.foodReplies.back().at("ok"), "global visibility accepts the published inventory world");
        fixture.SendFood(client, {{"kind", "foodReceipt"}, {"request", globalUse.at("request")}, {"eaten", false}});
        fixture.Script("globalThis.consumptionMode = 'restore';");
        fixture.UseFood(client, foodRow);
        Check(!client.foodReplies.back().at("ok").get<bool>() && inventory->Rows().empty(), "callback inventory restore invalidates approval");

        // Restoring an already empty inventory advances its generation without
        // changing its revision. Pot decisions must still be invalidated.
        const auto nextMeal = std::chrono::steady_clock::now() + std::chrono::milliseconds(1100);
        fixture.Wait([&] {
            return std::chrono::steady_clock::now() >= nextMeal;
        });
        Check(pots.Refill(guid, 0, 4), "refill for inventory restore regressions");
        fixture.Ready(client);
        revision = inventory->Revision();
        fixture.Reserve(client);
        Check(!client.replies.back().granted && pots.Snapshot(guid, 0).at("portions") == 4 && inventory->Revision() == revision, "pre-use restore with unchanged inventory rows preserves pot stock");
        fixture.Script("globalThis.consumptionMode = 'restoreEffects';");
        fixture.Ready(client);
        fixture.Reserve(client);
        Check(!client.replies.back().granted && pots.Snapshot(guid, 0).at("portions") == 4 && inventory->Revision() == revision, "effect-decision restore with unchanged inventory rows preserves pot stock");

        fixture.Script("globalThis.consumptionMode = 'allow';");
        fixture.Ready(client);
        const auto restoredRefusal = fixture.Reserve(client);
        Check(client.replies.back().granted && pots.Snapshot(guid, 0).at("portions") == 3, "reserve before inventory restore and native refusal");
        Check(fixture.server.GetInventory().ScriptSet(client.player->GetNetworkID(), {{"items", nlohmann::json::array()}}).at("ok"), "restore pending refusal inventory");
        fixture.Complete(client, restoredRefusal, false);
        fixture.Complete(client, restoredRefusal, false);
        Check(pots.Snapshot(guid, 0).at("portions") == 4, "explicit native refusal after inventory restore refunds pot stock once");

        const auto restoredPoison = fixture.Give(fixture.second, "banePoison", 1);
        Check(pots.Poison(fixture.second.player, guid, restoredPoison).at("ok"), "poison pending restore test serving");
        fixture.Ready(fixture.second);
        const auto restoredMeal = fixture.Reserve(fixture.second);
        Check(fixture.second.replies.back().granted, "reserve poisoned serving before inventory restore");
        Check(fixture.server.GetInventory().ScriptSet(fixture.second.player->GetNetworkID(), {{"items", nlohmann::json::array()}}).at("ok"), "restore pending successful receipt inventory");
        fixture.Complete(fixture.second, restoredMeal, true);
        fixture.Complete(fixture.second, restoredMeal, true);
        fixture.Script("if (globalThis.poisonCount !== 1 || globalThis.consumedCount !== 2) throw Error('restored pot receipt must not emit poison or consumption');");
        Check(pots.Snapshot(guid, 0).at("portions") == 3 && fixture.second.effects.empty(), "restored successful receipt spends its reserved serving without applying effects");

        fixture.Script("globalThis.statEvents = []; Events.on('playerStatsChanged', (player, changes) => { globalThis.statEvents.push(changes); });");
        fixture.server.GetPlayers().UpdateStats();
        client.player->soul.resources[0].current = 80;
        client.player->soul.conditions[4]        = 0.5F;
        fixture.server.GetPlayers().UpdateStats();
        fixture.server.GetPlayers().UpdateStats();
        fixture.Script("if (globalThis.statEvents.length !== 1 || !globalThis.statEvents[0].some(c => c.stat === 'health' && c.previous === 100 && c.current === 80) || !globalThis.statEvents[0].some(c => c.stat === 'poisoning')) throw Error('stat change batch');");
        ++client.player->soul.incarnation;
        client.player->soul.resources[0].current = 100;
        fixture.server.GetPlayers().UpdateStats();
        fixture.Script("if (globalThis.statEvents.length !== 1) throw Error('new life must establish baseline');");
        std::cout << "Cook pot RPC and inventory tests passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
