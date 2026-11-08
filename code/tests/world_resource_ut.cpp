#include "core/server.h"
#include "shared/features/world_builder/world_document.h"
#include "shared/game/vfx_catalog.h"
#include <core_modules.h>
#include <filesystem>
#include <fstream>
#include <integrations/server/scripting/module.h>
#include <iostream>
#include <networking/replication/replication_manager.h>
#include <scripting/engine.h>
#include <scripting/resource/resource_manager.h>
#include <stdexcept>

namespace {
    void Require(bool condition, const std::string &message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void Write(const std::filesystem::path &path, const std::string &text) {
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream file(path);
        file << text;
        Require(static_cast<bool>(file), "write fixture " + path.string());
    }

    nlohmann::json Export() {
        KCDC::Shared::Game::AreaDefinition area;
        area.id           = "test-area";
        area.name         = "Test area";
        area.shape.type   = KCDC::Shared::Game::AreaShapeType::Sphere;
        area.shape.radius = 5;
        area.collision    = true;
        KCDC::Shared::Game::LevelEditDefinition edit;
        edit.name          = "objects/test/brush.cgf";
        edit.at            = {1, 2, 3};
        edit.hidden        = true;
        nlohmann::json map = {{"format", "kcdc.world"}, {"version", 1}, {"level", "world-resource-tests"},
            {"objects", nlohmann::json::array({{{"model", "objects/manmade/common_decorations/corpses/bones_stash_shelf_a.cgf"}, {"position", {1, 2, 3}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 2, 3}}, {"physics", "static"}}})},
            {"effects",
                nlohmann::json::array({{{"effect", KCDC::Shared::Game::g_VfxEffects.front().name}, {"position", {1, 2, 3}}, {"rotation", {0, 0, 0, 1}}, {"tuning", {{"scale", 1}, {"count", 1}, {"speed", 1}, {"time", 1}, {"strength", -1}, {"pulse", 0}, {"prime", false}}}}})},
            {"world", nlohmann::json::array({KCDC::Shared::Game::LevelEditDefinitionToJson(edit)})}, {"areas", nlohmann::json::array({KCDC::Shared::Game::AreaDefinitionToJson(area)})}};
        KCDC::Shared::Game::PatrolRouteDefinition route;
        route.id            = "test.patrol";
        route.points        = {{{1, 2, 3}}, {{4, 5, 6}}};
        map["lights"]       = nlohmann::json::array({{{"position", {4, 5, 6}}, {"color", {0.2, 0.4, 0.8}}, {"intensity", 3}, {"radius", 9}, {"castShadows", true}}});
        map["version"]      = 2;
        map["patrolRoutes"] = nlohmann::json::array({KCDC::Shared::Game::PatrolRouteToJson(route)});
        return map;
    }
} // namespace

int RunWorldResourceSuite(bool emptyConfig) {
    KCDC::Server server;
    bool initialized = false;
    try {
        const auto document = Export();
        std::string fixtureError;
        Require(KCDC::Shared::WorldBuilder::ValidateContent(document, fixtureError), "valid fixture: " + fixtureError);
        Write("first.world.json", document.dump());
        auto second = document;
        second["lights"][0].erase("castShadows");
        second["world"]        = nlohmann::json::array();
        second["areas"]        = nlohmann::json::array();
        second["patrolRoutes"] = nlohmann::json::array();
        Write("other/first.world.json", second.dump());
        Write("server.json", R"({"mod":{"level":"world-resource-tests","required_dlc":[],"npcEnabled":false,"scoreboard":false,"world_resources":["first.world.json",{"name":"second","path":"other/first.world.json"}]}})");
        if (emptyConfig) {
            Write("server.json", R"({"mod":{"level":"world-resource-tests","required_dlc":[],"npcEnabled":false,"scoreboard":false,"world_resources":[]}})");
        }
        Write("resources/check/package.json", R"({"name":"check","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})");
        Write("resources/check/server.js", R"(
            globalThis.readyCount = 0;
            globalThis.removeSpawnedProp = false;
            globalThis.removeSpawnedEffect = false;
            Events.on("propSpawn", prop => { if (globalThis.removeSpawnedProp) prop.destroy(); });
            Events.on("vfxSpawn", effect => { if (globalThis.removeSpawnedEffect) effect.destroy(); });
            globalThis.startupEmpty = !WorldResource.ready && WorldResource.all().length === 0 && WorldResource.find("first") === null;
            Events.on("worldResourcesReady", () => {
                ++globalThis.readyCount;
                globalThis.readyNames = WorldResource.all().map(resource => resource.name).join(",");
                globalThis.readyInsideEvent = WorldResource.ready;
            });
        )");
        Framework::Integrations::Server::InstanceOptions options {};
        options.modSlug          = "world-resource-tests";
        options.modName          = "World resource tests";
        options.modVersion       = "2.0.0";
        options.gameName         = "world-resource-tests";
        options.gameVersion      = "1";
        options.bindHost         = "127.0.0.1";
        options.bindPort         = 28977;
        options.maxPlayers       = 4;
        options.bindPublicServer = false;
        options.verifyBuildToken = false;
        options.enableSignals    = false;
        options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "world-resource-tests"}, {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}, {"npcEnabled", Framework::Utils::ConfigFieldType::Bool, false},
             {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false}, {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}};
        char executable[]        = "world-resource-tests";
        char *serverArgs[]       = {executable};
        options.argc             = 1;
        options.argv             = serverArgs;
        const auto started       = server.Init(options);
        if (!started) {
            throw std::runtime_error("server initializes: " + started.GetError().message);
        }
        initialized                  = true;
        auto *engine                 = server.GetScriptingModule()->GetEngine();
        std::string assertionRuntime = "check";
        std::size_t assertionIndex   = 0;
        const auto check             = [&](const std::string &source) {
            const std::string path = "assertions/check-" + std::to_string(++assertionIndex) + ".js";
            Write(path, source);
            const bool succeeded = assertionRuntime.empty() ? engine->Execute(source, "world_resource_ut") : engine->ExecuteResourceFile(assertionRuntime, path);
            Require(succeeded, "script assertion: " + source + "\n" + engine->GetLastError());
        };
        check(R"(if (!globalThis.startupEmpty) throw Error("startup registry must be empty");)");
        if (emptyConfig) {
            server.LoadWorldResources();
            server.LoadWorldResources();
            check(R"(if (!WorldResource.ready || !readyInsideEvent || readyCount !== 1 || readyNames !== "" || WorldResource.all().length) throw Error("empty startup readiness");)");
            server.Shutdown();
            initialized = false;
            std::cout << "World resources: empty config emits readiness once\n";
            return 0;
        }

        const auto lightCount = [] {
            std::size_t count = 0;
            Framework::CoreModules::GetReplication()->ForEach<KCDC::Shared::Entities::LightEntity>([&count](auto *) {
                ++count;
            });
            return count;
        };
        Require(lightCount() == 0, "lights start empty");
        check(R"(
            (() => {
                const rejected = fn => { let refused = false; try { fn(); } catch { refused = true; } if (!refused) throw Error("invalid light input accepted"); };
                for (const position of [null, [], {}, {x:1,y:2}, {x:"1",y:2,z:3}, {x:NaN,y:0,z:0}, {x:Infinity,y:0,z:0}, {x:16385,y:0,z:0}]) rejected(() => Light.spawn(position));
                for (const options of [null, [], 7, {color:{x:1,y:1}}, {color:{x:-0.1,y:1,z:1}}, {color:{x:1,y:1,z:1.1}}, {color:{x:Infinity,y:0,z:0}}, {intensity:-1}, {intensity:101}, {intensity:NaN}, {intensity:"1"}, {radius:0}, {radius:101}, {radius:Infinity}, {castShadows:0}, {castShadows:1}, {castShadows:"true"}, {castShadows:null}, {castShadows:[]}, {castShadows:{}}]) rejected(() => Light.spawn(new Vector3(0,0,0), options));
                rejected(() => Light.spawn(new Vector3(0,0,0), {get intensity(){ throw Error("getter failed"); }}));
                const sentinel = Error("light vector getter sentinel");
                const preserves = fn => { let caught; try { fn(); } catch (error) { caught = error; } if (caught !== sentinel) throw Error("light getter exception replaced"); };
                preserves(() => Light.spawn({get x(){throw sentinel;}, y:0, z:0}));
                preserves(() => Light.spawn(new Vector3(0,0,0), {color:{get x(){throw sentinel;}, y:0, z:0}}));
            })();
        )");
        Require(lightCount() == 0, "invalid Light.spawn calls must not create entities");
        check(R"(
            (() => {
                globalThis.scriptLight = Light.spawn({x:10,y:20,z:30});
                if (!(scriptLight instanceof Light) || !(scriptLight instanceof Entity) || scriptLight.intensity !== 1 || scriptLight.radius !== 5 || scriptLight.castShadows !== false || Math.abs(scriptLight.color.y - 0.65) > 0.00001) throw Error("default Light handle");
                scriptLight.position = new Vector3(30,20,10);
                scriptLight.color = new Vector3(0.25,0.5,0.75);
                scriptLight.intensity = 4;
                scriptLight.radius = 12;
                scriptLight.castShadows = true;
                if (scriptLight.castShadows !== true) throw Error("enable shadows");
                scriptLight.castShadows = false;
                if (scriptLight.castShadows !== false) throw Error("disable shadows");
                scriptLight.castShadows = true;
                for (const value of [undefined, null, 0, 1, "true", [], {}, new Boolean(true)]) {
                    let refused = false; try { scriptLight.castShadows = value; } catch { refused = true; }
                    if (!refused || scriptLight.castShadows !== true) throw Error("nonboolean shadow update mutated entity");
                }
                const snapshot = () => JSON.stringify([scriptLight.position.x,scriptLight.position.y,scriptLight.position.z,scriptLight.color.x,scriptLight.color.y,scriptLight.color.z,scriptLight.intensity,scriptLight.radius,scriptLight.castShadows]);
                const before = snapshot();
                const sentinel = Error("light color getter sentinel");
                let caught;
                try { scriptLight.color = {get x(){throw sentinel;}, y:0.2, z:0.3}; } catch (error) { caught = error; }
                if (caught !== sentinel || snapshot() !== before) throw Error("light color getter exception replaced or mutated entity");
                for (const value of [undefined, null, [], {}, {x:0.1,y:0.2}, {x:"0.1",y:0.2,z:0.3}, {x:true,y:0.2,z:0.3}, {get x(){throw Error("color getter failed");},y:0.2,z:0.3}]) {
                    let refused = false; try { scriptLight.color = value; } catch { refused = true; }
                    if (!refused || snapshot() !== before) throw Error("malformed light color mutated entity");
                }
                for (const assign of [() => {scriptLight.intensity = NaN;}, () => {scriptLight.intensity = -1;}, () => {scriptLight.intensity = 101;}, () => {scriptLight.radius = Infinity;}, () => {scriptLight.radius = 0;}, () => {scriptLight.radius = 101;}, () => {scriptLight.color = new Vector3(1.1,0,0);}, () => {scriptLight.color = new Vector3(0,NaN,0);}, () => {scriptLight.position = new Vector3(Infinity,0,0);}, () => {scriptLight.position = new Vector3(0,16385,0);}]) {
                    let refused = false; try { assign(); } catch { refused = true; }
                    if (!refused || snapshot() !== before) throw Error("invalid light update mutated entity");
                }
                const clone = scriptLight.color; clone.x = 1;
                if (scriptLight.color.x !== 0.25) throw Error("color snapshot aliases light");
                const temporary = Light.spawn(new Vector3(0,0,0), {color:{x:1,y:0,z:0},intensity:0,radius:0.1,castShadows:true});
                if (!temporary.castShadows) throw Error("spawn shadow option");
                temporary.destroy(); temporary.destroy();
            })();
        )");
        Require(lightCount() == 1, "destroy removes only its light");
        check(R"(scriptLight.destroy();)");
        Require(lightCount() == 0, "explicit light destruction");

        auto &registry = server.GetWorldResources();
        std::string error;
        const auto rejected = [&](const nlohmann::json &paths, const std::string &reason) {
            Require(!registry.Load(paths, "world-resource-tests", error), "invalid export must fail");
            Require(error.find(reason) != std::string::npos, "expected " + reason + ", got " + error);
            Require(!error.empty() && !registry.IsReady() && registry.All().empty(), "failed startup publishes nothing");
            Require(lightCount() == 0, "failed startup rolls back every light");
            check(R"(if (readyCount !== 0 || WorldResource.ready) throw Error("failed load emitted readiness");)");
        };
        rejected(nlohmann::json::array({"missing.world.json"}), "Cannot read world resource");
        Write("bad.world.json", "{");
        rejected(nlohmann::json::array({"bad.world.json"}), "Expected a kcdc.world");
        auto invalidLight                   = document;
        invalidLight["lights"][0]["radius"] = -1;
        Write("invalid-light.world.json", invalidLight.dump());
        rejected(nlohmann::json::array({"first.world.json", "invalid-light.world.json"}), "Invalid light");
        invalidLight                             = document;
        invalidLight["lights"][0]["castShadows"] = "true";
        Write("invalid-shadow.world.json", invalidLight.dump());
        rejected(nlohmann::json::array({"first.world.json", "invalid-shadow.world.json"}), "Invalid light");
        auto wrongLevel     = second;
        wrongLevel["level"] = "wrong-level";
        Write("wrong.world.json", wrongLevel.dump());
        rejected(nlohmann::json::array({"wrong.world.json"}), "server uses");
        rejected(nlohmann::json::array({"first.world.json", "other/first.world.json"}), "Duplicate world resource name");
        rejected(nlohmann::json::array({"first.world.json", {{"name", "alias"}, {"path", "first.world.json"}}}), "Duplicate world resource file");
        rejected(nlohmann::json::array({{{"name", ""}, {"path", "first.world.json"}}}), "must not be empty");
        if (!std::filesystem::exists("alias.world.json")) {
            std::filesystem::create_hard_link("first.world.json", "alias.world.json");
        }
        rejected(nlohmann::json::array({"first.world.json", "alias.world.json"}), "Duplicate world resource file");
#ifdef _WIN32
        rejected(nlohmann::json::array({"first.world.json", "FIRST.WORLD.JSON"}), "Duplicate world resource file");
#endif
        check(R"(globalThis.removeSpawnedProp = true;)");
        rejected(nlohmann::json::array({"first.world.json"}), "removed by a spawn handler");
        check(R"(globalThis.removeSpawnedProp = false; globalThis.removeSpawnedEffect = true;)");
        rejected(nlohmann::json::array({"first.world.json"}), "removed by a spawn handler");
        check(R"(globalThis.removeSpawnedEffect = false;)");
        auto conflict            = document;
        conflict["patrolRoutes"] = nlohmann::json::array();
        Write("conflict.world.json", conflict.dump());
        rejected(nlohmann::json::array({"first.world.json", "conflict.world.json"}), "Two resources edit the same level object");
        check(R"(if (Prop.all().length || Vfx.all().length || LevelEdit.all().length || Area.all("script").length) throw Error("failed load leaked contents");)");

        auto duplicateRoutes     = document;
        duplicateRoutes["world"] = nlohmann::json::array();
        duplicateRoutes["areas"] = nlohmann::json::array();
        Write("duplicate-routes.world.json", duplicateRoutes.dump());
        rejected(nlohmann::json::array({"first.world.json", "duplicate-routes.world.json"}), "Duplicate patrol route id");
        check(R"(if (PatrolRoute.all().length || Prop.all().length || Vfx.all().length) throw Error("rollback leaked patrols or spawned contents");)");
        server.LoadWorldResources();
        Require(server.GetConfigError().empty() && registry.IsReady(), "config exports load");
        server.LoadWorldResources();
        Require(lightCount() == 2, "one light spawned per export");
        check(R"(
            if (readyCount !== 1 || !readyInsideEvent || readyNames !== "first,second") throw Error("ready event order or count");
            globalThis.first = WorldResource.find("first");
            globalThis.second = WorldResource.find("second");
            if (!(first instanceof WorldResource) || !first.loaded || first.level !== "world-resource-tests" || !first.path.endsWith("first.world.json")) throw Error("identity");
            if (WorldResource.find("FIRST") !== null || WorldResource.find("missing") !== null) throw Error("exact lookup");
            if (typeof WorldResource.load !== "undefined" || typeof first.reload !== "undefined" || typeof first.unload !== "undefined") throw Error("runtime resource mutations exposed");
            try { WorldResource.ready = false; } catch {}
            try { first.name = "changed"; } catch {}
            if (!WorldResource.ready || first.name !== "first") throw Error("identity or readiness is writable");
            let rejected = false;
            try { WorldResource.find(42); } catch { rejected = true; }
            if (!rejected) throw Error("find argument validation");
            const boundary = first.areas[0];
            if (!boundary.collision || !boundary.toJSON().collision) throw Error("collision export");
            boundary.enabled = false;
            boundary.collision = false;
            boundary.collision = true;
            boundary.enabled = true;
            boundary.virtualWorld = 4;
            boundary.virtualWorld = null;
            if (!boundary.setShape({radius: 6})) throw Error("collision reshape");
            if (boundary.setShape({radius: 0.1})) throw Error("collision accepted undersized sphere");
            if (boundary.collisionFloor !== false || boundary.collisionCeiling !== false || Math.abs(boundary.collisionThickness - 0.2) > 1e-6) throw Error("collision option defaults");
            boundary.collisionThickness = 0.5;
            if (Math.abs(boundary.toJSON().collisionThickness - 0.5) > 1e-6) throw Error("collision thickness export");
            let thicknessRejected = false;
            try { boundary.collisionThickness = 7; } catch (_) { thicknessRejected = true; }
            if (!thicknessRejected || Math.abs(boundary.collisionThickness - 0.5) > 1e-6) throw Error("collision thickness range");
            const plated = Area.create({id: "plated-box", type: "box", min: {x: -0.2, y: -0.2, z: 0}, max: {x: 0.2, y: 0.2, z: 3}, collision: true, collisionCeiling: true});
            if (!plated.collisionCeiling || plated.collisionFloor || !plated.toJSON().collisionCeiling) throw Error("collision plate option");
            plated.collisionFloor = true;
            if (!plated.collisionFloor) throw Error("collision floor assignment");
            let tooThick = false;
            try { plated.collisionThickness = 0.5; } catch (_) { tooThick = true; }
            if (!tooThick || Math.abs(plated.collisionThickness - 0.2) > 1e-6) throw Error("collision thickness accepted for a box narrower than its walls");
            let plateTypeRejected = false;
            try { Area.create({id: "bad-plate", type: "box", collision: true, collisionFloor: "yes"}); } catch (_) { plateTypeRejected = true; }
            if (!plateTypeRejected || Area.getById("bad-plate")) throw Error("collision plate type validation");
            plated.destroy();
            let collisionRejected = false;
            try { Area.create({id: "bad-boundary", type: "shape", points: [{x:0,y:0,z:0},{x:1,y:0,z:0},{x:0,y:1,z:0}], height: 0, collision: true, enabled: false}); } catch (_) { collisionRejected = true; }
            if (!collisionRejected || Area.getById("bad-boundary")) throw Error("disabled collision validation");
            if (first.props.length !== 1 || first.effects.length !== 1 || first.levelEdits.length !== 1 || first.areas.length !== 1) throw Error("membership");
            if (!(first.props[0] instanceof Prop) || !(first.effects[0] instanceof Vfx) || !(first.levelEdits[0] instanceof LevelEdit) || !(first.areas[0] instanceof Area)) throw Error("typed wrappers");
            if (first.lights.length !== 1 || !(first.lights[0] instanceof Light) || first.lights[0].radius !== 9 || first.lights[0].intensity !== 3 || first.lights[0].position.x !== 4 || first.lights[0].castShadows !== true || second.lights[0].castShadows !== false) throw Error("light export membership and settings");
            const exportedLight = first.lights[0];
            first.lights.length = 0;
            if (first.lights.length !== 1) throw Error("light array mutates membership");
            exportedLight.intensity = 7;
            if (second.lights[0].intensity !== 3) throw Error("light export isolation");
            exportedLight.destroy();
            if (first.lights.length || second.lights.length !== 1) throw Error("destroyed exported light retained");
            first.props.length = 0;
            if (first.props.length !== 1) throw Error("array mutates membership");
            first.props[0].destroy(); first.effects[0].destroy(); first.levelEdits[0].restore(); first.areas[0].destroy();
            if (!first.loaded || first.props.length || first.effects.length || first.levelEdits.length || first.areas.length) throw Error("dead members retained");
            if (second.props.length !== 1 || second.effects.length !== 1) throw Error("resource isolation");
        )");

        check(R"(
            const route = PatrolRoute.getById("test.patrol");
            if (!(route instanceof PatrolRoute) || first.patrolRoutes.length !== 1 || !route.exists) throw Error("route membership");
            let duplicate = false;
            try { PatrolRoute.create(route.toJSON()); } catch { duplicate = true; }
            if (!duplicate) throw Error("duplicate id accepted");
            const before = JSON.stringify(route.toJSON());
            for (const changes of [{id:"other"}, {mode:"invalid"}, {points:[]}, {waitSeconds:-1}, {radius:Infinity}, {points:[{position:{x:NaN,y:0,z:0}}]}]) {
                let refused = false; try { route.update(changes); } catch { refused = true; }
                if (!refused || JSON.stringify(route.toJSON()) !== before) throw Error("invalid update changed route");
            }
            if (!route.update({name:"Updated", mode:"pingPong", points:[{position:new Vector3(0,0,0), waitSeconds:2, speed:"jog",radius:0.5}]})) throw Error("update failed");
            const definition = route.toJSON(); definition.points[0].position.x = 999;
            if (route.toJSON().points[0].position.x !== 0 || route.name !== "Updated") throw Error("snapshot alias");
            const unavailable = Navigation.validatePatrol(route);
            if (unavailable.available || unavailable.complete || unavailable.legs.length) throw Error("missing mesh validation");
            let invalidActor = false; try { Navigation.validatePatrol(route, {actor:{}}); } catch { invalidActor = true; }
            if (!invalidActor) throw Error("invalid validation actor");
            const reentrant = PatrolRoute.create({id:"reentrant",points:[{position:new Vector3(0,0,0)}]});
            if (reentrant.update({toJSON(){ reentrant.destroy(); return {name:"gone"}; }})) throw Error("destroyed route updated");
            route.destroy();
            if (route.exists || route.toJSON() !== null || route.update({name:"gone"}) || PatrolRoute.getById("test.patrol") !== null || first.patrolRoutes.length) throw Error("destroy lifecycle");
            const replacement = PatrolRoute.create({id:"test.patrol",points:[{position:new Vector3(0,0,0)}]});
            if (route.exists || first.patrolRoutes.length) throw Error("stale handle aliases recreated id");
            replacement.destroy();
        )");

        Require(lightCount() == 1, "export member destruction updates replication");
        check(R"(globalThis.scriptLight = Light.spawn(new Vector3(7,8,9));)");
        Require(lightCount() == 2, "script and exported lights coexist");

        // Late resources take the ready branch without waiting for a replay.
        Write("late/late-check/package.json", R"({"name":"late-check","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})");
        Write("late/late-check/server.js", R"(
            const initialize = () => { globalThis.lateName = WorldResource.find("second").name; };
            if (WorldResource.ready) initialize();
            else Events.on("worldResourcesReady", initialize);
        )");
        auto *scripts = server.GetScriptingModule()->GetResourceManager();
        // DiscoverResource uses the same registration path as normal discovery.
        Require(static_cast<bool>(scripts->DiscoverResource("late/late-check")), "late resource is discovered");
        Require(static_cast<bool>(scripts->StartResource("late-check")), "late resource starts");
        assertionRuntime = "late-check";
        check(R"(if (lateName !== "second" || !WorldResource.ready) throw Error("late readiness");)");
        // Root handles outlive either script runtime; script globals do not.
        assertionRuntime.clear();
        check(R"(globalThis.first = WorldResource.find("first"); globalThis.second = WorldResource.find("second");)");
        Require(static_cast<bool>(scripts->StopResource("check")), "original script stops");
        Require(lightCount() == 1, "resource stop destroys its spawned lights only");
        check(R"(if (!second.loaded || second.props.length !== 1 || second.lights.length !== 1) throw Error("script stop removed server-owned export");)");
        Require(static_cast<bool>(scripts->StopResource("late-check")), "late script stops");
        Require(static_cast<bool>(scripts->StartResource("late-check")), "late script restarts");
        assertionRuntime = "late-check";
        check(R"(if (lateName !== "second" || !WorldResource.find("second").loaded) throw Error("restart readiness");)");
        assertionRuntime.clear();

        registry.Reset();
        Require(lightCount() == 0, "world-resource reset destroys exported lights");
        check(R"(if (WorldResource.ready || WorldResource.all().length || first.loaded || second.loaded || second.props.length || second.lights.length) throw Error("stale handles after reset");)");
        Require(registry.Load(nlohmann::json::array(), "world-resource-tests", error), "empty config is ready");
        check(R"(if (!WorldResource.ready || WorldResource.all().length) throw Error("empty readiness");)");
        registry.Reset();
        Require(registry.Load(nlohmann::json::array({"first.world.json"}), "world-resource-tests", error), "fresh registry can start again");
        check(R"(if (first.loaded || !WorldResource.find("first").loaded) throw Error("old handle aliases new resource");)");
        server.Shutdown();
        initialized = false;
        std::cout << "World resources: registry, config names, rollback, scripting collections, readiness and ownership passed\n";
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
