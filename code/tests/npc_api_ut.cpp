#include "npc_api_ut.h"

#include "core/server.h"
#include "features/npc/npc_inventory_module.h"
#include "features/npc/npc_verbs.h"

#include <core_modules.h>
#include <filesystem>
#include <fstream>
#include <integrations/server/scripting/module.h>
#include <iostream>
#include <networking/replication/replication_manager.h>
#include <scripting/engine.h>
#include <scripting/resource/resource_manager.h>
#include <stdexcept>

int RunNpcApiSuite() {
    KCDC::Server server;
    bool initialized = false;
    try {
        if (KCDC::Features::Npc::SpawnParams {}.locomotion != KCDC::Shared::Game::NpcLocomotion::Native || KCDC::Shared::Entities::NpcEntity {}.locomotion != KCDC::Shared::Game::NpcLocomotion::Native) {
            throw std::runtime_error("NPC spawn and replica defaults must be native");
        }
        std::ofstream("server.json") << R"({"mod":{"level":"npc-api-tests","required_dlc":[],"npcEnabled":true,"scoreboard":false,"world_resources":[]}})";
        std::filesystem::create_directories("resources/npc");
        std::ofstream("resources/npc/package.json") << R"({"name":"npc","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})";
        std::ofstream("resources/npc/server.js") << "// NPC API fixture\n";
        Framework::Integrations::Server::InstanceOptions options {};
        options.modSlug          = "npc-api-tests";
        options.modName          = "NPC API tests";
        options.modVersion       = "2.0.0";
        options.gameName         = "npc-api-tests";
        options.gameVersion      = "1";
        options.bindHost         = "127.0.0.1";
        options.bindPort         = 28979;
        options.maxPlayers       = 4;
        options.bindPublicServer = false;
        options.verifyBuildToken = false;
        options.enableSignals    = false;
        options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "npc-api-tests"}, {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}, {"npcEnabled", Framework::Utils::ConfigFieldType::Bool, true},
            {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false}, {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}};
        char executable[]        = "npc-api-tests";
        char *args[]             = {executable};
        options.argc             = 1;
        options.argv             = args;
        const auto started       = server.Init(options);
        if (!started) {
            throw std::runtime_error(started.GetError().message);
        }
        initialized          = true;
        auto *engine         = server.GetScriptingModule()->GetEngine();
        std::size_t sequence = 0;
        const auto check     = [&](const std::string &source) {
            const std::string path = "resources/npc/check-" + std::to_string(++sequence) + ".js";
            {
                std::ofstream file(path);
                file << "(() => {" << source << "})()";
            }
            if (!engine->ExecuteResourceFile("npc", path)) {
                throw std::runtime_error("NPC API assertion failed: " + engine->GetLastError());
            }
        };
        // Run locomotion checks before the inventory callbacks that remove spawns.
        check(R"(
            const assert = (value, message) => { if (!value) throw Error(message); };
            const rejects = (fn, message) => {
                let error;
                try { fn(); } catch (caught) { error = caught; }
                assert(error !== undefined, message);
                return String(error);
            };
            let spawnEvents = 0;
            Events.on("npcSpawn", () => spawnEvents++);
            let guid = 90000;
            for (const create of [options => { options.soul = "townsman"; return Npc.create(options); }, options => Npc.adopt(++guid, options)]) {
                for (const options of [{}, { locomotion: undefined }, { locomotion: "native" }, { locomotion: "kinematic" }]) {
                    const npc = create(options);
                    assert(npc.locomotion === (options.locomotion ?? "native"), "spawn/adopt locomotion default or explicit mode");
                    npc.remove();
                }
                for (const invalid of ["", "Native", "NATIVE", "Kinematic", "kinematc", " native", "native ", "walk", null, 1, true, {}, [], new String("native")]) {
                    const count = Npc.all().length;
                    const events = spawnEvents;
                    const error = rejects(() => create({ locomotion: invalid }), "invalid spawn/adopt accepted");
                    assert(error.includes("locomotion") && error.includes("'native'") && error.includes("'kinematic'"), "actionable locomotion error");
                    assert(Npc.all().length === count && spawnEvents === events, "invalid spawn/adopt mutated entities or emitted spawn");
                }
                let reads = 0;
                const npc = create({ get locomotion() { reads++; return reads === 1 ? "kinematic" : "native"; } });
                assert(reads === 1 && npc.locomotion === "kinematic", "locomotion accessor read once");
                npc.remove();
                const count = Npc.all().length;
                const events = spawnEvents;
                assert(rejects(() => create({ get locomotion() { throw Error("getter refused"); } }), "throwing getter accepted").includes("getter refused"), "getter exception preserved");
                assert(Npc.all().length === count && spawnEvents === events, "throwing getter spawned an NPC");
            }
            const adopted = Npc.adopt(++guid);
            assert(adopted.locomotion === "native", "adopt without options defaults native");
            adopted.locomotion = "kinematic";
            rejects(() => Npc.adopt(guid, { locomotion: "typo" }), "existing adoption ignored invalid option");
            assert(adopted.locomotion === "kinematic", "invalid adoption changed existing NPC");
            assert(Npc.adopt(guid, { locomotion: "native" }).id === adopted.id && adopted.locomotion === "kinematic", "adoption returns existing NPC without reconfiguring it");
            adopted.remove();

            const npc = Npc.create({ soul: "townsman", locomotion: undefined });
            for (const mode of ["native", "kinematic", "native"]) {
                npc.locomotion = mode;
                assert(npc.locomotion === mode, "supported mode round trip");
                assert(npc.patrol([new Vector3(0, 0, 0), new Vector3(5, 0, 0)]), "patrol accepted");
                assert(npc.locomotion === mode, "patrol preserves chosen locomotion");
                const patrol = JSON.stringify(npc.patrolState);
                for (const invalid of ["", "Native", "NATIVE", "Kinematic", "kinematc", " native", "native ", "walk", undefined, null, 1, true, {}, [], new String("native")]) {
                    rejects(() => { npc.locomotion = invalid; }, "invalid setter accepted");
                    assert(npc.locomotion === mode && JSON.stringify(npc.patrolState) === patrol, "invalid setter mutated mode or patrol");
                }
            }
            npc.remove();
            assert(Npc.all().length === 0, "locomotion tests leaked NPCs");
        )");
        check(R"(
            globalThis.assert = (value, message) => { if (!value) throw Error(message); };
            globalThis.human = Npc.create({ soul: "bandit", wearing: ["ce1953df-9f10-4138-8fb1-393a11474553"], position: new Vector3(0, 0, 0) });
            globalThis.animal = Npc.create({ soul: Npc.animals().find(row => row.actorClass === "Hare").soul, position: new Vector3(0, 0, 0) });
            assert(human.getAnimalLoot() === null, "human has no animal drops");
            assert(human.getInventory().ready && human.getInventory().items.some(row => row.equipped), "authored human gear is stock");
            assert(animal.getInventory().ready && animal.getInventory().items.length === 0, "core creates empty animal stock");
            const candidates = animal.getAnimalLoot();
            assert(candidates.length === 3 && candidates[0].amount === 8, "candidate API is an instance method");
            globalThis.meat = candidates[0].item;
            assert(Inventory.set(animal, { items: [{ item: meat, amount: 8 }], harvested: false }).ok, "gamemode prepares stock");
            const before = JSON.stringify(animal.getInventory());
            assert(!Inventory.set(animal, { items: [{ item: meat, amount: 9, equipped: 1 }] }).ok, "food cannot be equipped");
            assert(!Inventory.set(animal, { items: [], harvested: 1 }).ok, "harvested must be boolean");
            assert(JSON.stringify(animal.getInventory()) === before, "invalid restores are atomic");
            const row = animal.getInventory().items[0];
            assert(Inventory.transfer(animal, human, { units: [{ id: row.id, amount: 3 }] }).ok, "NPC-to-NPC transfer");
            assert(animal.getInventory().items[0].amount === 5, "partial transfer retains remainder");
            const revision = animal.getInventory().revision;
            assert(!Inventory.remove(animal, { units: [{ id: row.id, amount: 1 }], revision: revision - 1 }).ok, "stale revision refused");
            assert(animal.getInventory().items[0].amount === 5, "refused removal preserves stock");
            assert(Inventory.set(animal, { items: animal.getInventory().items, harvested: true }).ok && animal.getInventory().harvested, "saved harvest state restores");
            assert(Inventory.set(human, { items: [{ item: "dfea5d01-b25c-414a-9ab4-6911a5f82118", amount: 1000, equipped: 1000 }] }).ok, "restore a full quiver");
            assert(human.getInventory().items[0].equipped === 1000 && human.wearing.length === 1, "equipment publishes a quiver once regardless of unit count");
            assert(Inventory.set(human, { items: human.getInventory().items }).ok && human.getInventory().items[0].equipped === 1000, "quiver save/restore round trip");
            assert(human.wear(["ce1953df-9f10-4138-8fb1-393a11474553", "ce1953df-9f10-4138-8fb1-393a11474553"]), "dress from duplicate authored classes");
            assert(human.wearing.length === 1 && human.getInventory().items.filter(row => row.equipped).length === 1, "wearing reflects normalized inventory equipment");
            assert(human.setOutfit("4d3b3797-4f92-4337-8a41-94ed0714f924"), "UC_Godwin remains a supported preset");
            globalThis.outfitted = Npc.create({ soul: "bandit", outfit: "4d3b3797-4f92-4337-8a41-94ed0714f924", position: new Vector3(0, 0, 0) });
            assert(outfitted.getInventory().ready && outfitted.getInventory().items.some(row => row.equipped), "authored outfit establishes stock including supported equipment");
        )");
        KCDC::Features::Npc::NpcInventoryModule::Get().Update();
        check(R"(
            globalThis.removedChanges = 0;
            globalThis.removedReady = 0;
            globalThis.invalidEvent = false;
            globalThis.victim = Npc.create({ soul: "bandit", wearing: ["ce1953df-9f10-4138-8fb1-393a11474553"], position: new Vector3(0, 0, 0) });
            globalThis.victimId = victim.id;
            Inventory.add(victim, { item: meat, amount: 1 });
            Events.on("npcInventoryChanged", npc => {
                if (!npc) { invalidEvent = true; return; }
                if (npc.id === victimId) { removedChanges++; npc.remove(); }
            });
            Events.on("npcInventoryReady", npc => {
                if (!npc) { invalidEvent = true; return; }
                if (npc.id === victimId) removedReady++;
            });
        )");
        KCDC::Features::Npc::NpcInventoryModule::Get().Update();
        check(R"(
            assert(removedChanges === 1 && removedReady === 0 && !invalidEvent && Npc.getById(victimId) === null, "removal cancels queued notifications");
            const count = Npc.all().length;
            Events.on("npcSpawn", npc => npc.remove());
            let refused = false;
            try { Npc.create({ soul: "bandit", wearing: ["ce1953df-9f10-4138-8fb1-393a11474553"], position: new Vector3(0, 0, 0) }); } catch { refused = true; }
            assert(refused && Npc.all().length === count, "removal from npcSpawn never returns freed native storage");
        )");
        auto *replication         = Framework::CoreModules::GetReplication();
        std::uint64_t destroyedId = 0;
        replication->ForEach<KCDC::Shared::Entities::NpcEntity>([&](auto *npc) {
            if (destroyedId == 0) {
                destroyedId = npc->GetNetworkID();
            }
        });
        check("globalThis.stale = Npc.getById(" + std::to_string(destroyedId) + ");");
        replication->DestroyEntity(replication->GetEntity<KCDC::Shared::Entities::NpcEntity>(destroyedId));
        check(R"(
            assert(Inventory.get(stale) === null && stale.getInventory() === null, "stale owners cannot read retained stock before the next tick");
            assert(!Inventory.add(stale, { item: meat, amount: 1 }).ok, "stale owners cannot mutate stock");
        )");
        std::vector<std::uint64_t> owned;
        replication->ForEach<KCDC::Shared::Entities::NpcEntity>([&](auto *npc) {
            owned.push_back(npc->GetNetworkID());
        });
        if (owned.empty() || !server.GetScriptingModule()->GetResourceManager()->StopResource("npc")) {
            throw std::runtime_error("resource teardown fixture failed");
        }
        KCDC::Features::Npc::NpcInventoryModule::Get().Update();
        for (const auto id : owned) {
            if (replication->GetEntity<KCDC::Shared::Entities::NpcEntity>(id) != nullptr || KCDC::Features::Npc::NpcInventoryModule::Get().Find(id) != nullptr || !KCDC::Features::Npc::NpcInventoryModule::Get().ScriptGet(id).is_null()) {
                throw std::runtime_error("resource teardown retained NPC stock");
            }
        }
        server.Shutdown();
        initialized = false;
        std::cout << "NPC API: locomotion, stock, restore, transfers and callback removal passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (initialized) {
            server.Shutdown();
        }
        return 1;
    }
}
