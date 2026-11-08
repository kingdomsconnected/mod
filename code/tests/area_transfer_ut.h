#pragma once

#include "core/server.h"

#include <filesystem>
#include <fstream>
#include <integrations/server/scripting/module.h>
#include <iostream>
#include <scripting/engine.h>
#include <stdexcept>
#include <string>

// Every server resource runs in an isolate of its own, so an area handed to another resource -- as every
// areaEnter and areaExit is -- is copied across. It has to arrive as the same Area, not as an empty object.
inline int RunAreaTransferSuite() {
    KCDC::Server server;
    bool initialized = false;
    try {
        std::ofstream("server.json") << R"({"mod":{"level":"trosecko","required_dlc":[],"npcEnabled":false,"scoreboard":false,"world_resources":[]}})";
        for (const std::string name : {"area-sender", "area-receiver"}) {
            std::filesystem::create_directories("resources/" + name);
            std::ofstream("resources/" + name + "/package.json") << R"({"name":")" << name << R"(","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})";
        }
        std::ofstream("resources/area-sender/server.js") << "";
        std::ofstream("resources/area-receiver/server.js") << R"JS(
            globalThis.received = [];
            Events.on("areaTransferProbe", (area) => { received.push({ isArea: area instanceof Area, id: area.id, type: area.type, exists: area.exists }); });
        )JS";
        Framework::Integrations::Server::InstanceOptions options {};
        options.modSlug          = "area-transfer-tests";
        options.modName          = "Area transfer tests";
        options.modVersion       = "1.0.0";
        options.gameName         = "area-transfer-tests";
        options.gameVersion      = "1";
        options.bindHost         = "127.0.0.1";
        options.bindPort         = 28984;
        options.maxPlayers       = 4;
        options.bindPublicServer = false;
        options.verifyBuildToken = false;
        options.enableSignals    = false;
        options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "trosecko"}, {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}, {"npcEnabled", Framework::Utils::ConfigFieldType::Bool, false},
             {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false}, {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}};
        char executable[]        = "area-transfer-tests";
        char *args[]             = {executable};
        options.argc             = 1;
        options.argv             = args;
        const auto started       = server.Init(options);
        if (!started) {
            throw std::runtime_error(started.GetError().message);
        }
        initialized = true;

        std::ofstream("resources/area-sender/emit.js") << R"JS(
            const area = Area.create({ id: "transfer-probe", type: "sphere", position: new Vector3(1, 2, 3), radius: 2 });
            Events.emit("areaTransferProbe", area);
            area.destroy();
            Events.emit("areaTransferProbe", area);
        )JS";
        std::ofstream("resources/area-receiver/assertions.js") << R"JS(
            (() => {
                const assert = (condition, message) => { if (!condition) throw Error(message); };
                assert(received.length === 2, `expected two deliveries, got ${received.length}`);
                const [live, gone] = received;
                assert(live.isArea && live.id === "transfer-probe" && live.type === "sphere" && live.exists, `a live area arrived as ${JSON.stringify(live)}`);
                assert(gone.isArea && !gone.exists, `a destroyed area arrived as ${JSON.stringify(gone)}`);
            })();
        )JS";
        auto *engine = server.GetScriptingModule()->GetEngine();
        if (!engine->ExecuteResourceFile("area-sender", "resources/area-sender/emit.js")) {
            throw std::runtime_error("Area transfer emit failed: " + engine->GetLastError());
        }
        if (!engine->ExecuteResourceFile("area-receiver", "resources/area-receiver/assertions.js")) {
            throw std::runtime_error("Area transfer assertions failed: " + engine->GetLastError());
        }
        server.Shutdown();
        initialized = false;
        std::cout << "Area transfer: live and destroyed areas reach another resource as Area handles\n";
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
