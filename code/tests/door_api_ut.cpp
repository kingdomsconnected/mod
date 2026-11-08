#include "door_api_ut.h"

#include "core/server.h"
#include "features/door/door_service.h"
#include "features/door/gate_service.h"
#include "shared/features/door/door_definition.h"
#include "shared/features/door/door_rpc.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <filesystem>
#include <fstream>
#include <integrations/server/scripting/module.h>
#include <iostream>
#include <networking/replication/replication_manager.h>
#include <scripting/engine.h>
#include <scripting/resource/resource_manager.h>
#include <stdexcept>

// A door and a gate the catalog places in kutnohorsko: a house door by the spawn and the
// spawn's portcullis.
namespace {
    constexpr std::uint64_t kDoorGuid = 0x4dd4235513a51a93ULL;
    constexpr std::uint64_t kGateGuid = 0x4b5d68eb986fda15ULL;
} // namespace

int RunDoorApiSuite() {
    KCDC::Server server;
    bool initialized = false;
    try {
        std::ofstream("server.json") << R"({"mod":{"level":"kutnohorsko","required_dlc":[],"scoreboard":false,"world_resources":[]}})";
        std::filesystem::create_directories("resources/doors");
        std::ofstream("resources/doors/package.json") << R"({"name":"doors","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})";
        std::ofstream("resources/doors/server.js") << "// Door API fixture\n";
        // A doorInteract handler that takes the door it is asked about away: a report must not touch it afterwards.
        // It refuses to let another open.
        std::filesystem::create_directories("resources/interact");
        std::ofstream("resources/interact/package.json") << R"({"name":"interact","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})";
        std::ofstream("resources/interact/server.js") << R"(Events.on("doorInteract", (player, door, action) => { if (door.name === "doomed") { door.destroy(); } else if (door.name === "refused" && action === "open") { return false; } });)";
        Framework::Integrations::Server::InstanceOptions options {};
        options.modSlug          = "door-api-tests";
        options.modName          = "Door API tests";
        options.modVersion       = "2.0.0";
        options.gameName         = "door-api-tests";
        options.gameVersion      = "1";
        options.bindHost         = "127.0.0.1";
        options.bindPort         = 28981;
        options.maxPlayers       = 4;
        options.bindPublicServer = false;
        options.verifyBuildToken = false;
        options.enableSignals    = false;
        options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "kutnohorsko"}, {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}, {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false},
            {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}};
        char executable[]  = "door-api-tests";
        char *args[]       = {executable};
        options.argc       = 1;
        options.argv       = args;
        const auto started = server.Init(options);
        if (!started) {
            throw std::runtime_error(started.GetError().message);
        }
        initialized = true;

        auto &doors = server.GetDoors();
        auto &gates = server.GetGates();
        if (doors.Find(kDoorGuid, MafiaNet::VIRTUAL_WORLD_GLOBAL) == nullptr || gates.Collect(MafiaNet::VIRTUAL_WORLD_GLOBAL).empty()) {
            throw std::runtime_error("the level's doors and gates are built in the global world at boot");
        }

        // A script asks for world 5's copies, so its resource owns them.
        std::ofstream("resources/doors/ask.js") << R"((() => {
            if (Door.find("4dd4235513a51a93", 5) === null || Gate.find("4b5d68eb986fda15", 5) === null) {
                throw Error("world 5 copies");
            }
        })())";
        if (!server.GetScriptingModule()->GetEngine()->ExecuteResourceFile("doors", "resources/doors/ask.js")) {
            throw std::runtime_error("Door API assertion failed: " + server.GetScriptingModule()->GetEngine()->GetLastError());
        }
        auto *door = doors.Find(kDoorGuid, 5);
        if (door == nullptr || gates.Collect(5).size() != 1) {
            throw std::runtime_error("the world 5 copies are indexed");
        }
        const std::uint64_t doorId = door->GetNetworkID();
        const std::uint64_t gateId = gates.Collect(5).front()->GetNetworkID();

        // Stopping the resource destroys them; the services must not hand them out again.
        if (!server.GetScriptingModule()->GetResourceManager()->StopResource("doors")) {
            throw std::runtime_error("resource teardown fixture failed");
        }
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication->GetEntityByNetworkID(doorId) != nullptr || replication->GetEntityByNetworkID(gateId) != nullptr) {
            throw std::runtime_error("the stopped resource destroyed the copies it owned");
        }
        if (doors.Find(kDoorGuid, 5) != nullptr || !doors.Collect(5).empty() || !gates.Collect(5).empty()) {
            throw std::runtime_error("a destroyed copy is still indexed");
        }
        gates.Update();
        if (doors.Find(kDoorGuid, MafiaNet::VIRTUAL_WORLD_GLOBAL) == nullptr || gates.Collect(MafiaNet::VIRTUAL_WORLD_GLOBAL).empty()) {
            throw std::runtime_error("the global copies outlive the resource");
        }

        // Asked for again, each is built anew.
        auto *rebuiltDoor = doors.Resolve(kDoorGuid, 5);
        auto *rebuiltGate = gates.Resolve(kGateGuid, 5);
        if (rebuiltDoor == nullptr || rebuiltGate == nullptr || rebuiltDoor->GetNetworkID() == doorId || rebuiltGate->GetNetworkID() == gateId || doors.Find(kDoorGuid, 5) != rebuiltDoor || gates.Collect(5).size() != 1) {
            throw std::runtime_error("a destroyed copy is built anew when asked for");
        }

        // Two hung doors in the global world, worked by a body in world 1, which
        // sees the global world: one a handler destroys mid-report, one it leaves.
        const auto hang = [&doors](const char *name, const glm::vec3 &position) {
            KCDC::Shared::Game::DoorDefinition definition;
            definition.model    = "lvl1_door_a_left";
            definition.position = position;
            definition.name     = name;
            std::string error;
            auto *hung = doors.Spawn(definition, MafiaNet::VIRTUAL_WORLD_GLOBAL, error);
            if (hung == nullptr) {
                throw std::runtime_error("hanging a door: " + error);
            }
            return hung->GetNetworkID();
        };
        const std::uint64_t doomedId  = hang("doomed", glm::vec3(100.0F, 100.0F, 50.0F));
        const std::uint64_t keptId    = hang("kept", glm::vec3(120.0F, 100.0F, 50.0F));
        const std::uint64_t refusedId = hang("refused", glm::vec3(140.0F, 100.0F, 50.0F));
        const std::uint64_t barredId  = hang("barred", glm::vec3(160.0F, 100.0F, 50.0F));
        auto *actor                   = replication->CreateEntity<KCDC::Shared::Entities::HumanEntity>();
        if (actor == nullptr) {
            throw std::runtime_error("the acting body could not be created");
        }
        actor->SetVirtualWorld(1);
        const auto work = [&](std::uint64_t doorId, const glm::vec3 &at) {
            auto *door = replication->GetEntity<KCDC::Shared::Entities::DoorEntity>(doorId);
            KCDC::Shared::RPC::DoorInteraction interaction;
            interaction.levelHash = KCDC::Shared::RPC::DoorLevelHash("kutnohorsko");
            interaction.doorGuid  = door->doorGuid;
            interaction.event     = KCDC::Shared::RPC::DoorEvent::Manipulation;
            interaction.open      = true;
            interaction.locked    = door->locked;
            interaction.fromFront = true;
            actor->position       = at + glm::vec3(1.0F, 0.0F, 0.0F);
            doors.ProcessInteraction(interaction, *actor);
        };
        work(doomedId, glm::vec3(100.0F, 100.0F, 50.0F));
        if (replication->GetEntityByNetworkID(doomedId) != nullptr) {
            throw std::runtime_error("the handler destroyed the door the report was about");
        }
        work(keptId, glm::vec3(120.0F, 100.0F, 50.0F));
        auto *kept = replication->GetEntity<KCDC::Shared::Entities::DoorEntity>(keptId);
        if (kept == nullptr || !kept->open) {
            throw std::runtime_error("a body in world 1 opens a door hung in the global world");
        }
        // Refused, the door stays shut, and its state goes back out so the reporter's client puts its own door back.
        const std::uint32_t refusedToken = replication->GetEntity<KCDC::Shared::Entities::DoorEntity>(refusedId)->stateToken;
        work(refusedId, glm::vec3(140.0F, 100.0F, 50.0F));
        auto *refused = replication->GetEntity<KCDC::Shared::Entities::DoorEntity>(refusedId);
        if (refused == nullptr || refused->open || !refused->changedByServer || refused->stateToken == refusedToken) {
            throw std::runtime_error("a doorInteract handler returning false keeps the door shut and corrects the reporter");
        }

        // A script makes a door unusable: the flag replicates, and a report from a client that let the use through anyway is refused.
        std::ofstream("resources/interact/bar.js") << R"((() => {
            const door = Door.all().find(candidate => candidate.name === "barred");
            if (door === undefined || door.usable !== true) {
                throw Error("a door starts usable");
            }
            door.usable = false;
            if (door.usable !== false) {
                throw Error("door.usable reads back what was set");
            }
        })())";
        if (!server.GetScriptingModule()->GetEngine()->ExecuteResourceFile("interact", "resources/interact/bar.js")) {
            throw std::runtime_error("Door API assertion failed: " + server.GetScriptingModule()->GetEngine()->GetLastError());
        }
        auto *barred = replication->GetEntity<KCDC::Shared::Entities::DoorEntity>(barredId);
        if (barred == nullptr || barred->usable) {
            throw std::runtime_error("door.usable reaches the replicated door");
        }
        work(barredId, glm::vec3(160.0F, 100.0F, 50.0F));
        if (barred->open || !barred->changedByServer) {
            throw std::runtime_error("a report against an unusable door is refused and corrected");
        }
        doors.SetUsable(*barred, true);
        work(barredId, glm::vec3(160.0F, 100.0F, 50.0F));
        if (!barred->open) {
            throw std::runtime_error("a door made usable again opens");
        }
        replication->DestroyEntity(actor);

        server.Shutdown();
        initialized = false;
        std::cout << "Door API: world copies a stopped resource destroyed are dropped and rebuilt; a door a handler destroys mid-report is left alone; a global hung door works from world 1; a refused open keeps the door shut; an unusable door refuses a report\n";
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
