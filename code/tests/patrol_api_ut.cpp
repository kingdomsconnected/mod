#include "core/server.h"
#include "features/door/door_service.h"
#include "features/npc/npc_verbs.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <integrations/server/scripting/module.h>
#include <iostream>
#include <scripting/engine.h>
#include <stdexcept>
#include <thread>

int RunPatrolApiSuite() {
    KCDC::Server server;
    bool initialized = false;
    try {
        std::ofstream("server.json") << R"({"mod":{"level":"patrol-tests","required_dlc":[],"npcEnabled":true,"scoreboard":false,"world_resources":[]}})";
        std::filesystem::create_directories("resources/patrol");
        std::ofstream("resources/patrol/package.json") << R"({"name":"patrol","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})";
        std::ofstream("resources/patrol/server.js") << "// Patrol API fixture\n";
        Framework::Integrations::Server::InstanceOptions options {};
        options.modSlug          = "patrol-tests";
        options.modName          = "Patrol tests";
        options.modVersion       = "2.0.0";
        options.gameName         = "patrol-tests";
        options.gameVersion      = "1";
        options.bindHost         = "127.0.0.1";
        options.bindPort         = 28978;
        options.maxPlayers       = 4;
        options.bindPublicServer = false;
        options.verifyBuildToken = false;
        options.enableSignals    = false;
        options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "patrol-tests"}, {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}, {"npcEnabled", Framework::Utils::ConfigFieldType::Bool, true},
             {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false}, {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}};
        char executable[]        = "patrol-tests";
        char *args[]             = {executable};
        options.argc             = 1;
        options.argv             = args;
        const auto started       = server.Init(options);
        if (!started) {
            throw std::runtime_error(started.GetError().message);
        }
        initialized                = true;
        {
            using KCDC::Shared::Game::DoorPassage;
            KCDC::Shared::Entities::NpcEntity actorType;
            actorType.actorClass = std::string("NPC_Female");
            if (!actorType.CanOpenDoors()) {
                throw std::runtime_error("humanoid door capability must compare class text");
            }
            actorType.actorClass = "Horse";
            if (actorType.CanOpenDoors()) {
                throw std::runtime_error("animals cannot open doors");
            }
            auto &doors = server.GetDoors();
            doors.Install("kutnohorsko");
            const auto &placement = *std::find_if(KCDC::Shared::Game::g_DoorPlacements.begin(), KCDC::Shared::Game::g_DoorPlacements.end(), [](const auto &door) { return std::string_view(door.level) == "kutnohorsko"; });
            const glm::vec3 approach(placement.positionX - placement.forwardX, placement.positionY - placement.forwardY, placement.positionZ);
            auto *global   = doors.Resolve(placement.guid, MafiaNet::VIRTUAL_WORLD_GLOBAL);
            auto *instance = doors.Resolve(placement.guid, 17);
            if (!global || !instance) {
                throw std::runtime_error("door materialization");
            }
            doors.SetLock(*global, true);
            doors.SetLock(*instance, false);
            if (doors.CanNavigate(placement.guid, MafiaNet::VIRTUAL_WORLD_GLOBAL) || !doors.CanNavigate(placement.guid, 17)) {
                throw std::runtime_error("door navigation must use the actor's world");
            }
            if (doors.PreparePassage(placement.guid, MafiaNet::VIRTUAL_WORLD_GLOBAL, approach, 1000) != DoorPassage::Blocked || global->open || !global->locked) {
                throw std::runtime_error("NPC unlocked a locked door");
            }
            const auto token = instance->stateToken;
            if (doors.PreparePassage(placement.guid, 17, approach, 1000) != DoorPassage::Opening || !instance->open || !instance->changedByServer || !instance->openedFromFront || instance->stateToken == token) {
                throw std::runtime_error("NPC door opening must replicate from the approach side");
            }
            const auto openingToken = instance->stateToken;
            if (doors.PreparePassage(placement.guid, 17, approach, 1200) != DoorPassage::Opening || instance->stateToken != openingToken) {
                throw std::runtime_error("another NPC must share the existing swing");
            }
            if (doors.PreparePassage(placement.guid, 17, approach, 2500) != DoorPassage::Ready || global->open) {
                throw std::runtime_error("door swing completion or world isolation");
            }
            doors.SetOpen(*instance, false, false);
            doors.SetLock(*instance, true);
            if (doors.PreparePassage(placement.guid, 17, approach, 2600) != DoorPassage::Blocked) {
                throw std::runtime_error("a newly locked door must block traversal");
            }
        }
        auto *engine               = server.GetScriptingModule()->GetEngine();
        std::size_t assertionIndex = 0;
        const auto check           = [&](const std::string &source) {
            const std::string path = "resources/patrol/check-" + std::to_string(++assertionIndex) + ".js";
            {
                std::ofstream file(path);
                file << "(() => {" << source << "})()";
            }
            if (!engine->ExecuteResourceFile("patrol", path)) {
                throw std::runtime_error("Patrol API assertion failed: " + engine->GetLastError());
            }
        };
        check(R"(
            globalThis.npc = Npc.create({soul:"guard", position:new Vector3(0,0,0)});
            if (!npc) throw Error("create NPC");
            globalThis.route = PatrolRoute.create({id:"guard.route", mode:"pingPong", speed:"walk",points:[{position:new Vector3(0,0,0)},{position:new Vector3(5,0,0),speed:"jog"}]});
            if (!npc.patrol(route) || npc.patrolState.routeId !== route.id || npc.patrolState.waypointIndex !== 0) throw Error("route handle patrol");
            route.update({points:[{position:new Vector3(99,0,0)}]});
            route.destroy();
            if (npc.patrolState.routeId !== "guard.route") throw Error("destroy cancelled snapshot");
            let unknown = false; try { npc.patrol("missing"); } catch { unknown = true; }
            if (!unknown || npc.patrolState.routeId !== "guard.route") throw Error("invalid route replaced order");
            npc.hold(); if (npc.patrolState !== null) throw Error("hold retained patrol");
            if (!npc.patrol([new Vector3(0,0,0), new Vector3(5,0,0)], {loop:true,waitSeconds:3})) throw Error("legacy patrol refused");
            if (npc.patrolState.routeId !== null) throw Error("anonymous route id");
            npc.hold(); npc.remove();
            const doomed = Npc.create({soul:"guard", position:new Vector3(0,0,0)});
            if (doomed.patrol([new Vector3(0,0,0)], { get speed() { doomed.remove(); return "walk"; } })) throw Error("removed NPC accepted patrol");
        )");
        check(R"(
            globalThis.outcomes = [];
            globalThis.oldOutcomes = [];
            Events.on("patrolWaypoint", (actor, info) => { outcomes.push(["waypoint", actor.id, info.status, info.routeId]); });
            Events.on("patrolFinished", (actor, info) => { outcomes.push(["finished", actor.id, info.status, info.routeId]); });
            Events.on("npcIntentDone", (actor, status) => oldOutcomes.push(status));
            globalThis.walker = Npc.create({soul:"guard",position:new Vector3(0,0,0)});
            globalThis.once = PatrolRoute.create({id:"once",mode:"once",points:[{position:new Vector3(0,0,0)}]});
            walker.patrol(once);
        )");
        for (int tick = 0; tick < 3; ++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            KCDC::Features::Npc::Npcs::Update();
        }
        check(R"(
            if (outcomes.length !== 2 || outcomes[0][0] !== "waypoint" || outcomes[0][2] !== "reached" || outcomes[1][0] !== "finished" || outcomes[1][2] !== "completed") throw Error("waypoint and completion events: " + JSON.stringify(outcomes));
            if (oldOutcomes.join() !== "reached" || walker.patrolState !== null) throw Error("legacy completion changed");
            globalThis.other = Npc.create({soul:"guard",position:new Vector3(0,0,0)});
            Events.on("patrolWaypoint", actor => { if (actor.id === walker.id) { other.remove(); actor.remove(); } });
            walker.patrol(once); other.patrol(once);
        )");
        for (int tick = 0; tick < 3; ++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            KCDC::Features::Npc::Npcs::Update();
        }
        check(R"(if (Npc.all().length) throw Error("callback removal left actors"); once.destroy();)");
        check(R"(if (PatrolRoute.all().length) throw Error("test leaked definitions");)");
        server.Shutdown();
        initialized = false;
        std::cout << "Patrol API tests passed\n";
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
