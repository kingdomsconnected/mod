#pragma once

#include "core/server.h"
#include "shared/game/gathering_sources.h"

#include <filesystem>
#include <fstream>
#include <integrations/server/scripting/module.h>
#include <iostream>
#include <scripting/engine.h>
#include <stdexcept>

inline int RunGatheringApiSuite() {
    KCDC::Server server;
    bool initialized = false;
    try {
        const auto &sources = KCDC::Shared::Game::GatheringSources("trosecko");
        if (sources.size() != 758392 || KCDC::Shared::Game::GatheringSources("kutnohorsko").size() != 2362314 || KCDC::Shared::Game::GatheringSources("klaster").size() != 419736 || !KCDC::Shared::Game::GatheringSources("unknown").empty()) {
            throw std::runtime_error("authored herb catalog counts");
        }
        const auto &seed = sources.front();
        std::ofstream("server.json") << R"({"mod":{"level":"trosecko","required_dlc":[],"npcEnabled":false,"scoreboard":false,"world_resources":[]}})";
        std::filesystem::create_directories("resources/herbs");
        std::ofstream("resources/herbs/package.json") << R"({"name":"herbs","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})";
        std::ofstream("resources/herbs/server.js") << "globalThis.herbSeed = " << nlohmann::json({{"x", seed.position.x}, {"y", seed.position.y}, {"z", seed.position.z}}).dump() << ";\n";
        Framework::Integrations::Server::InstanceOptions options {};
        options.modSlug          = "herb-api-tests";
        options.modName          = "Herb API tests";
        options.modVersion       = "2.0.0";
        options.gameName         = "herb-api-tests";
        options.gameVersion      = "1";
        options.bindHost         = "127.0.0.1";
        options.bindPort         = 28983;
        options.maxPlayers       = 4;
        options.bindPublicServer = false;
        options.verifyBuildToken = false;
        options.enableSignals    = false;
        options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "trosecko"}, {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}, {"npcEnabled", Framework::Utils::ConfigFieldType::Bool, false},
             {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false}, {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}};
        char executable[]        = "herb-api-tests";
        char *args[]             = {executable};
        options.argc             = 1;
        options.argv             = args;
        const auto started       = server.Init(options);
        if (!started) {
            throw std::runtime_error(started.GetError().message);
        }
        initialized            = true;
        const char *assertions = R"JS(
            (() => {
                const assert = (condition, message) => { if (!condition) throw Error(message); };
                const rejects = fn => { let rejected = false; try { fn(); } catch { rejected = true; } assert(rejected, "invalid argument accepted"); };
                const p = new Vector3(herbSeed.x, herbSeed.y, herbSeed.z);
                const initial = Gathering.getHerbs(p, 1);
                assert(initial.length > 0, "catalog query requires no players");
                assert(initial.every(h => Number.isInteger(h.kind) && typeof h.item === "string" && h.position instanceof Vector3), "herb row types");
                const area = Area.create({id: "herb-api", type: "sphere", position: p, radius: 1, virtualWorld: 17});
                assert(area.getHerbs().length === initial.length, "area and radius parity");
                assert(area.getHerbs(0).length === 0, "area world restriction");
                for (const world of [-1, 1.5, NaN, Infinity, 4294967296, "17", null]) {
                    rejects(() => area.removeHerbs(world));
                    rejects(() => Gathering.removeHerbs(p, 1, world));
                }
                for (const radius of [-1, NaN, Infinity, 100001, "1", null]) rejects(() => Gathering.removeHerbs(p, radius));
                rejects(() => Gathering.removeHerbs({x: Infinity, y: 0, z: 0}, 1));
                rejects(() => Gathering.removeHerbs(p));
                rejects(() => area.removeHerbs(17, 18));
                assert(area.getHerbs().length === initial.length, "invalid calls mutated plants");
                assert(area.removeHerbs(4294967295) === 0, "bound area must not affect global bucket");
                assert(Gathering.removeHerbs(p, 1, 4294967295) === initial.length, "default player global bucket accepted");
                assert(Gathering.getHerbs(p, 1, 4294967295).length === 0, "global bucket removal");
                assert(Gathering.getHerbs(p, 1, 0).length === initial.length, "global bucket does not mean all worlds");
                const unbound = Area.create({id: "herb-global", type: "sphere", position: p, radius: 1});
                assert(unbound.respawnHerbs(4294967295) === initial.length, "unbound area restores global bucket");
                assert(unbound.removeHerbs(4294967295) === initial.length, "unbound area removes global bucket");
                assert(Gathering.respawnAll(4294967295) === initial.length, "existing respawn global bucket");
                unbound.destroy();
                let harvestEvents = 0;
                Events.on("gatheringHarvest", () => { ++harvestEvents; });
                Events.on("gatheringHarvested", () => { ++harvestEvents; });
                area.enabled = false;
                assert(area.removeHerbs() === initial.length, "disabled area geometry");
                assert(area.removeHerbs() === 0 && area.getHerbs().length === 0, "idempotent removal");
                assert(Gathering.getHerbs(p, 1).length === initial.length, "world isolation");
                Gathering.setRespawnTime(1, 1);
                Gathering.resetRespawnTime();
                assert(area.getHerbs().length === 0, "timing leaves removed plants hidden");
                assert(area.respawnHerbs() === initial.length && area.respawnHerbs() === 0, "shape restoration");
                assert(Gathering.removeHerbs(p, 1, 17) === initial.length, "radius removal");
                area.destroy();
                assert(area.getHerbs().length === 0 && area.removeHerbs() === 0 && area.respawnHerbs() === 0, "destroyed handle");
                assert(Gathering.getHerbs(p, 1, 17).length === 0, "destroy does not restore");
                assert(Gathering.respawn(p, 1, 17) === initial.length, "radius restoration");
                const exact = Gathering.getHerbs(p, 0);
                assert(exact.length > 0, "zero radius includes exact source");
                assert(Gathering.removeHerbs(p, 0) === exact.length, "zero radius removal");
                assert(Gathering.respawnAll() === exact.length, "global restoration");
                const started = Date.now();
                assert(Gathering.removeHerbs(p, 100000, 23) === 758392, "whole-level removal count");
                assert(Gathering.removeHerbs(p, 100000, 23) === 0, "whole-level repeat");
                assert(Gathering.respawnAll(23) === 758392, "whole-level restoration count");
                console.log("Whole-level herb clear/repeat/restore:", Date.now() - started, "ms");
                assert(harvestEvents === 0, "script removal emitted harvest events");
            })();
        )JS";
        std::ofstream("resources/herbs/assertions.js") << assertions;
        auto *engine = server.GetScriptingModule()->GetEngine();
        if (!engine->ExecuteResourceFile("herbs", "resources/herbs/assertions.js")) {
            throw std::runtime_error("Herb API assertions failed: " + engine->GetLastError());
        }
        server.Shutdown();
        initialized = false;
        std::cout << "Herb API: authored catalogs, bindings, radius/area parity, validation, worlds, removal and restoration passed\n";
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
