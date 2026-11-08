#include "core/ui/native_ui/native_ui_assets.h"
#include "features/prop/prop_editor_document.h"
#include "features/prop/prop_editor_map_travel.h"
#include "shared/features/world_builder/mcp_server.h"
#include "shared/features/world_builder/world_builder_access.h"
#include "shared/features/world_builder/world_document.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Only file locations depend on the DLL; the document and disk writer are real.
const std::string &KCDC::Core::NativeUIAssets::ModuleDirectory() {
    static const std::string directory = (std::filesystem::current_path() / "world-builder-tests").string();
    return directory;
}

namespace {
    void Require(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void TestMapTravel() {
        using KCDC::Features::Prop::MapTravelClick;
        MapTravelClick click;
        click.ActiveArea(true);
        Require(!click.Pointer(false, 10, 10), "Mouse-up without a press selected a map point");
        Require(!click.Pointer(true, 10, 10), "Mouse-down selected before release");
        Require(click.Pointer(false, 12, 12), "A map click was lost");
        click.Pointer(true, 10, 10);
        click.Pointer(true, 30, 10);
        Require(!click.Pointer(false, 10, 10), "Drag returning to its start teleported");
        click.ActiveArea(false);
        click.Pointer(true, 10, 10);
        click.ActiveArea(true);
        Require(!click.Pointer(false, 10, 10), "Press outside the map teleported");
        click.Pointer(true, 10, 10);
        click.ActiveArea(false);
        click.Pointer(true, 10, 10);
        click.ActiveArea(true);
        Require(!click.Pointer(false, 10, 10), "Leaving the active map area did not cancel the gesture");
        click.Pointer(true, 10, 10);
        click.ActiveArea(false);
        click.ActiveArea(true);
        Require(!click.Pointer(false, 12, 10), "Native leave/re-enter events between pointer messages allowed travel");
        click.Pointer(true, 10, 10);
        click.Pointer(false, 10, 10);
        click.ActiveArea(false);
        click.ActiveArea(true);
        Require(!click.TakeClick(), "Native active-area exit retained a pending click");
        click.Pointer(true, 10, 10);
        Require(!click.Pointer(false, 40, 10), "Motion on release was treated as a click");
        click.Pointer(true, 10, 10);
        click.Reset();
        Require(!click.Pointer(false, 10, 10), "Focus loss retained an unfinished gesture");
        click.Pointer(true, 10, 10);
        click.Pointer(false, 10, 10);
        click.Pointer(false, 20, 10);
        Require(!click.TakeClick(), "Click then movement retained a stale destination");
        click.Pointer(true, 10, 10);
        click.Pointer(false, 10, 10);
        click.Pointer(true, 10, 10);
        Require(!click.TakeClick(), "A new press retained the previous click");
        click.Pointer(false, 10, 10);
        Require(click.TakeClick() && !click.TakeClick(), "A stationary click must be consumed exactly once");

        using KCDC::Shared::WorldBuilder::McpServer;
        const auto tools = KCDC::Features::Prop::MapTravelTools();
        Require(tools.size() == 4, "Map travel tools missing");
        std::string error;
        for (const auto &tool : tools) {
            const bool travel = tool.at("name") == "wb_map_travel";
            const auto &schema = tool.at("inputSchema");
            Require(McpServer::ValidateArguments(schema, travel ? nlohmann::json {{"position", {120, 240}}} : nlohmann::json::object(), error), "Valid map tool input rejected");
            Require(!McpServer::ValidateArguments(schema, {{"unknown", true}}, error), "Unknown map tool input accepted");
            if (travel) {
                for (const auto &point : {nlohmann::json::array({-1, 0}), nlohmann::json::array({0, 40000}), nlohmann::json::array({1, 2, 3}), nlohmann::json::array({"1", 2})}) {
                    Require(!McpServer::ValidateArguments(schema, {{"position", point}}, error), "Malformed map destination accepted");
                }
            }
        }
    }

    void TestRecentProjects() {
        for (const auto *text : {"", "broken", "{}", "null", "42", "[]"}) {
            Require(KCDC::Features::Prop::ParseRecentProjects(text).empty(), "Invalid or empty project history should be empty");
        }
        const std::string latest = "E:/maps/market-\xC4\x8D.project.json";
        const std::string older  = "E:\\maps\\older.project.json";
        nlohmann::json history   = {nullptr, "", 12, latest, false, latest, std::string("bad\0path", 8), older};
        Require(KCDC::Features::Prop::ParseRecentProjects(history.dump()) == std::vector<std::string> {latest, older}, "Recent projects lost order, Unicode, or accepted invalid/duplicate paths");
        for (int i = 0; i < 20; ++i) {
            history.push_back("project-" + std::to_string(i) + ".project.json");
        }
        const auto paths = KCDC::Features::Prop::ParseRecentProjects(history.dump());
        Require(paths.size() == 10 && paths.front() == latest && paths.back() == "project-7.project.json", "Recent projects did not retain the ten newest unique paths");
    }
} // namespace

void TestWorldBuilderGizmo();
void TestWorldBuilderAreaGeometry();
void TestWorldBuilderAreaEditor();
void TestWorldBuilderPatrolEditor();

int RunWorldBuilderSuite() {
    try {
        TestMapTravel();
        TestWorldBuilderGizmo();
        TestWorldBuilderAreaGeometry();
        TestWorldBuilderAreaEditor();
        TestWorldBuilderPatrolEditor();
        TestRecentProjects();
        for (const bool enabled : {true, false}) {
            KCDC::Shared::RPC::SetWorldBuilderAccess sent;
            sent.enabled = enabled;
            MafiaNet::BitStream wire;
            sent.Serialize(&wire, true);
            KCDC::Shared::RPC::SetWorldBuilderAccess received;
            received.Serialize(&wire, false);
            Require(received.IsSane() && received.enabled == enabled, "Builder grant/revoke did not round-trip");
        }
        MafiaNet::BitStream empty;
        KCDC::Shared::RPC::SetWorldBuilderAccess malformed;
        malformed.Serialize(&empty, false);
        Require(!malformed.IsSane(), "Empty builder permission accepted");

        KCDC::Features::Prop::MapDocument document;
        document.level           = "kutnohorsko";
        document.exportDirectory = "exports/market";
        document.objects.push_back({"objects/a\\quoted\"mesh.cgf", {1, 2, 3}, {0, 0, 0, 1}, {2, 3, 4}, KCDC::Common::Spawn::PropPhysics::Static, "market"});
        KCDC::Features::Prop::DocumentEffect effect;
        effect.effect             = "test.fire";
        effect.position           = {10, 20, 30};
        effect.tuning.countScale  = 2.5F;
        effect.tuning.pulsePeriod = 3;
        effect.tuning.prime       = true;
        effect.group              = "market";
        document.effects.push_back(effect);
        document.views.push_back({"front", {1, 2, 3}, 45, 10});
        document.camera = KCDC::Features::Prop::DocumentView {"", {120, 240, 36}, -120, -35};
        KCDC::Shared::Game::AreaDefinition area;
        area.id           = "market";
        area.name         = "Market";
        area.shape.type   = KCDC::Shared::Game::AreaShapeType::Sphere;
        area.shape.radius = 5;
        document.areas.push_back(area);
        document.areaGroups.emplace(area.id, "market");
        KCDC::Shared::Game::PatrolRouteDefinition route;
        route.id     = "market.guard";
        route.name   = "Market patrol";
        route.mode   = KCDC::Shared::Game::PatrolMode::PingPong;
        route.points = {{{4, 5, 6}, KCDC::Shared::Game::NpcSpeed::Jog, 3.0F, 0.75F}, {{10, 20, 30}}};
        document.patrolRoutes.push_back(route);
        KCDC::Shared::Game::LevelEditDefinition edit;
        edit.kind   = KCDC::Shared::Game::LevelEditKind::Brush;
        edit.name   = "objects/test/brush.cgf";
        edit.at     = {1, 2, 3};
        edit.hidden = true;
        document.world.push_back(edit);

        std::string error;
        const auto text = KCDC::Features::Prop::SerializeProject(document);
        KCDC::Features::Prop::MapDocument loaded;
        Require(KCDC::Features::Prop::ParseProject(text, loaded, error), error.c_str());
        Require(KCDC::Features::Prop::SerializeProject(loaded) == text, "Project round trip lost data");
        Require(KCDC::Features::Prop::ProjectToJson(loaded) == nlohmann::json::parse(text), "The unsaved check would call an unchanged project dirty");
        Require(loaded.objects[0].group == "market" && loaded.effects[0].group == "market", "Groups did not survive");
        Require(loaded.areaGroups.at("market") == "market", "Area group did not survive");
        Require(loaded.camera && loaded.camera->position.x == 120 && loaded.camera->position.y == 240 && loaded.camera->position.z == 36 && loaded.camera->yawDegrees == -120 && loaded.camera->pitchDegrees == -35, "Last camera transform did not survive");
        auto oldProject = nlohmann::json::parse(text);
        oldProject["editor"].erase("camera");
        Require(KCDC::Features::Prop::ParseProject(oldProject.dump(), loaded, error) && !loaded.camera, "Legacy project without camera did not load");
        auto badCamera                       = nlohmann::json::parse(text);
        badCamera["editor"]["camera"]["yaw"] = "wrong";
        Require(!KCDC::Features::Prop::ParseProject(badCamera.dump(), loaded, error), "Malformed camera angle accepted");
        badCamera                                 = nlohmann::json::parse(text);
        badCamera["editor"]["camera"]["position"] = {1, 2};
        Require(!KCDC::Features::Prop::ParseProject(badCamera.dump(), loaded, error), "Malformed camera position accepted");
        const nlohmann::json content = KCDC::Features::Prop::MapDocumentToJson(document);
        Require(content == nlohmann::json::parse(KCDC::Features::Prop::SerializeMapDocument(document)), "Map JSON and map text disagree");
        auto resource = KCDC::Shared::WorldBuilder::ExportResource(content);
        Require(!resource.contains("views") && !resource.contains("editor") && !resource["objects"][0].contains("group"), "Export leaked project metadata");
        Require(!resource["areas"][0].contains("group"), "Export leaked area groups");
        Require(!KCDC::Features::Prop::ParseProject(resource.dump(), loaded, error), "Runtime resource accepted as project");
        nlohmann::json read;
        Require(KCDC::Shared::WorldBuilder::ReadResource(resource.dump(), read, error), error.c_str());
        Require(!KCDC::Shared::WorldBuilder::ReadResource(text, read, error), "Project accepted as runtime resource");
        auto legacy       = resource;
        legacy["version"] = 1;
        legacy.erase("patrolRoutes");
        Require(KCDC::Shared::WorldBuilder::ReadResource(legacy.dump(), read, error), "Version 1 world without routes rejected");
        Require(resource["patrolRoutes"].size() == 1 && resource["patrolRoutes"][0]["mode"] == "pingPong", "Export lost route");
        auto invalidRoute = resource;
        invalidRoute["patrolRoutes"].push_back(invalidRoute["patrolRoutes"][0]);
        Require(!KCDC::Shared::WorldBuilder::ReadResource(invalidRoute.dump(), read, error), "Duplicate route accepted");
        invalidRoute                                                = resource;
        invalidRoute["patrolRoutes"][0]["points"][0]["waitSeconds"] = -1;
        Require(!KCDC::Shared::WorldBuilder::ReadResource(invalidRoute.dump(), read, error), "Invalid waypoint wait accepted");
        auto bad       = resource;
        bad["version"] = 3;
        Require(!KCDC::Shared::WorldBuilder::ReadResource(bad.dump(), read, error), "Future format accepted");
        bad                           = resource;
        bad["objects"][0]["rotation"] = {0, 0, 0, 0};
        Require(!KCDC::Shared::WorldBuilder::ReadResource(bad.dump(), read, error), "Zero quaternion accepted");
        bad                                  = resource;
        bad["effects"][0]["tuning"]["count"] = "wrong";
        Require(!KCDC::Shared::WorldBuilder::ReadResource(bad.dump(), read, error), "Malformed effect accepted");
        bad                        = resource;
        bad["objects"][0]["scale"] = {1, -1, 1};
        Require(!KCDC::Shared::WorldBuilder::ReadResource(bad.dump(), read, error), "Negative scale accepted");
        bad = resource;
        bad["areas"].push_back(bad["areas"][0]);
        Require(!KCDC::Shared::WorldBuilder::ReadResource(bad.dump(), read, error), "Duplicate area accepted");

        const auto calls = KCDC::Shared::WorldBuilder::ApiCalls(content);
        Require(calls.find("Prop.spawn(\"objects/a\\\\quoted\\\"mesh.cgf\"") != std::string::npos, "Script strings not escaped");
        Require(calls.find("\"x\":2.0,\"y\":3.0,\"z\":4.0") != std::string::npos, "Nonuniform scale lost");
        Require(calls.find("\"countScale\":2.5") != std::string::npos && calls.find("\"pulsePeriod\":3.0") != std::string::npos, "Particle tuning not mapped to API");
        Require(calls.find("LevelEdit.apply(") != std::string::npos, "Level edit API export missing");
        Require(calls.find("PatrolRoute.create(") != std::string::npos, "Patrol API export missing");
        Require(calls.find("Area.create(") != std::string::npos, "Area API export missing");
        Require(calls.find("\"group\"") == std::string::npos && calls.find("\"camera\"") == std::string::npos, "API calls leaked editor preferences");

        std::string blueprint;
        Require(!KCDC::Features::Prop::SerializeBlueprint(document, blueprint, error), "Blueprint accepted level-specific edits");
        auto selection = document;
        selection.world.clear();
        Require(KCDC::Features::Prop::SerializeBlueprint(selection, blueprint, error), error.c_str());
        Require(KCDC::Features::Prop::ParseBlueprint(blueprint, loaded, error), error.c_str());
        Require(loaded.level.empty() && loaded.views.empty(), "Blueprint retained level or camera views");
        Require(!loaded.camera && loaded.areaGroups.at("market") == "market", "Blueprint lost area groups or retained the project camera");
        Require(loaded.objects[0].position.x == 0 && loaded.objects[0].position.y == 0 && loaded.objects[0].position.z == 0, "Blueprint pivot not relative");
        KCDC::Features::Prop::OffsetDocument(loaded, {100, 200, 300});
        Require(loaded.effects[0].position.x == 109 && loaded.effects[0].position.y == 218 && loaded.effects[0].position.z == 327, "Blueprint placement changed relative effect offsets");
        Require(loaded.patrolRoutes[0].points[0].position == glm::vec3(103, 203, 303), "Blueprint lost relative route position");
        Require(loaded.areas[0].shape.position == glm::vec3(99, 198, 297), "Blueprint placement changed relative area offsets");
        Require(loaded.objects[0].scale.y == 3 && loaded.effects[0].tuning.countScale == 2.5F, "Blueprint lost authored settings");
        Require(!KCDC::Features::Prop::ParseBlueprint(text, loaded, error), "Project accepted as blueprint");
        Require(!KCDC::Features::Prop::ParseBlueprint(resource.dump(), loaded, error), "Runtime resource accepted as blueprint");
        auto invalidBlueprint       = nlohmann::json::parse(blueprint);
        invalidBlueprint["version"] = 3;
        Require(!KCDC::Features::Prop::ParseBlueprint(invalidBlueprint.dump(), loaded, error), "Future blueprint format accepted");
        Require(!KCDC::Features::Prop::SerializeBlueprint({}, blueprint, error), "Empty blueprint accepted");

        const auto path = KCDC::Features::Prop::MapFiles::Directory() + "/overwrite-\xC4\x8D.project.json";
        Require(KCDC::Features::Prop::MapFiles::WritePath(path, text, error), error.c_str());
        document.objects[0].position.x = 99;
        const auto changed             = KCDC::Features::Prop::SerializeProject(document);
        Require(KCDC::Features::Prop::MapFiles::WritePath(path, changed, error), error.c_str());
        std::string readText;
        Require(KCDC::Features::Prop::MapFiles::ReadPath(path, readText, error) && readText == changed, "Saving over an existing project failed");
        Require(!std::filesystem::exists(std::filesystem::u8path(path + ".tmp")), "Successful save left staging file");
        Require(!KCDC::Features::Prop::MapFiles::WritePath(path, std::string(32 * 1024 * 1024 + 1, 'x'), error), "Saved a file the reader would refuse");
        Require(KCDC::Features::Prop::MapFiles::ReadPath(path, readText, error) && readText == changed, "Rejected save damaged the existing project");
        const auto browserRoot = std::filesystem::u8path(KCDC::Features::Prop::MapFiles::Directory()) / "folder-browser";
        std::filesystem::create_directories(browserRoot / "z-last");
        std::filesystem::create_directories(browserRoot / std::filesystem::u8path("a-\xC4\x8D"));
        std::ofstream(browserRoot / "not-a-folder.txt") << "ignored";
        const auto browserUtf8 = browserRoot.u8string();
        const std::string browserPath(browserUtf8.begin(), browserUtf8.end());
        std::string directory;
        std::vector<std::string> entries;
        Require(KCDC::Features::Prop::MapFiles::ListDirectories(browserPath, directory, entries, error), error.c_str());
        Require(entries == std::vector<std::string> {"a-\xC4\x8D", "z-last"}, "Folder browser must sort folders, preserve Unicode and exclude files");
        Require(std::filesystem::u8path(directory).is_absolute(), "Folder browser must resolve an absolute output path");
        Require(KCDC::Features::Prop::MapFiles::ListDirectories(browserPath + "/a-\xC4\x8D", directory, entries, error) && entries.empty(), "Cannot browse an empty Unicode folder");
        Require(!KCDC::Features::Prop::MapFiles::ListDirectories(browserPath + "/missing", directory, entries, error), "Missing folder accepted");
        Require(directory.empty() && entries.empty() && !error.empty(), "Failed navigation kept a stale selectable folder");
        Require(!KCDC::Features::Prop::MapFiles::ListDirectories(browserPath + "/not-a-folder.txt", directory, entries, error), "File accepted as export folder");
        std::filesystem::remove(browserRoot / "not-a-folder.txt");
        std::filesystem::remove(browserRoot / "z-last");
        std::filesystem::remove(browserRoot / std::filesystem::u8path("a-\xC4\x8D"));
        std::filesystem::remove(browserRoot);
        std::filesystem::remove(std::filesystem::u8path(path));
        std::cout << "World Builder: area editing and selection, gizmo recovery, project/blueprint round trips, recent project history, format separation, validation, API copying, Unicode overwrite and folder browsing passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
