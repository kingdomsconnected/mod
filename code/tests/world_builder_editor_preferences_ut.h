#pragma once

#ifdef _WIN32
#include "features/prop/prop_editor_document.h"
#include "features/prop/prop_editor_environment.h"
#include "features/prop/prop_editor_favorites.h"
#include "features/prop/prop_editor_history.h"
#include "features/prop/prop_editor_objects.h"
#include "features/prop/prop_editor_viewport.h"
#include "shared/features/world_builder/mcp_server.h"

#include <cmath>
#include <limits>

MODULE(world_builder_editor_preferences, {
    IT("prefers a solid bucket or building over overlapping decoration bounds", {
        KCDC::Features::Prop::WorldPick solid, decoration;
        solid.kind            = KCDC::Features::Prop::WorldPickKind::Entity;
        solid.entityId        = 42;
        solid.distance        = 5.0F;
        decoration.kind       = KCDC::Features::Prop::WorldPickKind::RenderNode;
        decoration.distance   = 2.0F;
        decoration.boundsOnly = true;
        EQUALS(KCDC::Features::Prop::PreferWorldPick(decoration, solid, true), false);
        EQUALS(KCDC::Features::Prop::PreferWorldPick(solid, decoration, true), true);
        // Resolving an anonymous layer collider produces the same surface
        // priority even for a large brush whose box encloses the camera.
        solid.kind          = KCDC::Features::Prop::WorldPickKind::RenderNode;
        decoration.distance = 0.0F;
        EQUALS(KCDC::Features::Prop::PreferWorldPick(decoration, solid, true), false);
        EQUALS(KCDC::Features::Prop::PreferWorldPick(decoration, solid, false), true);
    });

    IT("retains non-solid fallback ahead of terrain and unresolved collision", {
        KCDC::Features::Prop::WorldPick surface, decoration;
        decoration.kind       = KCDC::Features::Prop::WorldPickKind::RenderNode;
        decoration.boundsOnly = true;
        decoration.distance   = 3.0F;
        EQUALS(KCDC::Features::Prop::PreferWorldPick(decoration, surface, true), true);
        for (const auto kind : {KCDC::Features::Prop::WorldPickKind::Terrain, KCDC::Features::Prop::WorldPickKind::Unknown}) {
            surface.kind     = kind;
            surface.distance = 4.0F;
            EQUALS(KCDC::Features::Prop::PreferWorldPick(decoration, surface, true), true);
            surface.distance = 2.0F;
            EQUALS(KCDC::Features::Prop::PreferWorldPick(decoration, surface, true), false);
        }
        EQUALS(KCDC::Features::Prop::PreferWorldPick({}, decoration, true), false);
    });

    IT("orders equally precise level hits by distance with stable ties", {
        KCDC::Features::Prop::WorldPick first, next;
        first.kind = next.kind = KCDC::Features::Prop::WorldPickKind::Entity;
        first.distance         = 5.0F;
        for (const bool boundsOnly : {false, true}) {
            first.boundsOnly = next.boundsOnly = boundsOnly;
            next.distance                      = 4.5F;
            EQUALS(KCDC::Features::Prop::PreferWorldPick(next, first, true), true);
            next.distance = 5.0F;
            EQUALS(KCDC::Features::Prop::PreferWorldPick(next, first, true), false);
            next.distance = 5.5F;
            EQUALS(KCDC::Features::Prop::PreferWorldPick(next, first, true), false);
        }
    });

    IT("persists bounds picking and defaults older projects to solid picking", {
        KCDC::Features::Prop::MapDocument document, restored;
        document.level                      = "kutnohorsko";
        document.viewport.pickPreferPhysics = false;
        std::string error;
        auto project = KCDC::Features::Prop::ProjectToJson(document);
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), true);
        EQUALS(restored.viewport.pickPreferPhysics, false);
        project["editor"]["viewport"].erase("pickPreferPhysics");
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), true);
        EQUALS(restored.viewport.pickPreferPhysics, true);
        const auto before            = restored.viewport.ToJson();
        auto invalid                 = before;
        invalid["pickPreferPhysics"] = "false";
        EQUALS(KCDC::Features::Prop::EditorViewport::FromJson(invalid, restored.viewport, error), false);
        EQUALS(restored.viewport.ToJson() == before, true);
    });

    IT("restores duplicate layer membership after undoing and redoing its earlier assignment", {
        KCDC::Features::Prop::EditorLayers layers;
        KCDC::Features::Prop::EditorHistory history;
        std::string error;
        const auto custom = layers.Create("Buildings", error);
        KCDC::Features::Prop::HistoryAction assignment;
        assignment.kind         = KCDC::Features::Prop::HistoryAction::Kind::Layers;
        assignment.layersBefore = std::make_shared<const KCDC::Features::Prop::EditorLayers>(layers);
        EQUALS(layers.Assign({"prop:1", "effect:1"}, custom, error), true);
        assignment.layersAfter = std::make_shared<const KCDC::Features::Prop::EditorLayers>(layers);
        history.Record({"Assign", 0, {assignment}}, &layers);
        EQUALS(layers.Assign({"prop:2", "effect:2"}, custom, error), true);
        KCDC::Features::Prop::HistoryAction prop, effect;
        prop.kind        = KCDC::Features::Prop::HistoryAction::Kind::Place;
        prop.object.id   = 2;
        effect.kind      = KCDC::Features::Prop::HistoryAction::Kind::EffectPlace;
        effect.effect.id = 2;
        history.Record({"Duplicate", 0, {prop, effect}}, &layers);
        EQUALS(history.UndoStep(&layers).has_value(), true);
        EQUALS(layers.Members().contains("prop:2"), false);
        EQUALS(layers.Members().contains("effect:2"), false);
        EQUALS(layers.Members().size(), std::size_t(2));
        EQUALS(history.UndoStep(&layers).has_value(), true);
        EQUALS(layers.LayerOf("prop:2"), std::string("default"));
        EQUALS(history.RedoStep(&layers).has_value(), true);
        EQUALS(history.RedoStep(&layers).has_value(), true);
        EQUALS(layers.LayerOf("prop:2"), custom);
        EQUALS(layers.LayerOf("effect:2"), custom);
        prop.kind   = KCDC::Features::Prop::HistoryAction::Kind::Delete;
        effect.kind = KCDC::Features::Prop::HistoryAction::Kind::EffectDelete;
        history.Record({"Delete", 0, {prop, effect}}, &layers);
        EQUALS(layers.Members().contains("prop:2"), false);
        EQUALS(layers.Members().contains("effect:2"), false);
        EQUALS(layers.Members().size(), std::size_t(2));
        EQUALS(history.UndoStep(&layers).has_value(), true);
        EQUALS(layers.LayerOf("prop:2"), custom);
        EQUALS(layers.LayerOf("effect:2"), custom);
        EQUALS(layers.Members().size(), std::size_t(4));
        EQUALS(history.RedoStep(&layers).has_value(), true);
        EQUALS(layers.Members().contains("prop:2"), false);
        EQUALS(layers.Members().contains("effect:2"), false);
        EQUALS(layers.Members().size(), std::size_t(2));
    });

    IT("does not recycle folder identifiers when undo is followed by a new creation", {
        KCDC::Features::Prop::FavoriteFolders favorites;
        std::string error;
        const auto initial = favorites;
        const auto first   = favorites.Add("First", error);
        EQUALS(favorites.Set("objects/test/first.cgf", true, first, error), true);
        const auto created = favorites;
        EQUALS(favorites.Restore(initial), true);
        EQUALS(favorites.Find(first) == nullptr, true);
        EQUALS(favorites.ActiveId(), KCDC::Features::Prop::FavoriteFolders::kRoot);
        const auto replacement = favorites.Add("Replacement", error);
        EQUALS(replacement > first, true);
        EQUALS(favorites.Restore(created), true);
        EQUALS(favorites.Contains("objects/test/first.cgf", first), true);
        EQUALS(favorites.Find(replacement) == nullptr, true);
        EQUALS(favorites.Add("After redo", error) > replacement, true);
        const auto unchanged = favorites.ToJson();
        EQUALS(favorites.Restore(favorites), false);
        EQUALS(favorites.ToJson() == unchanged, true);
    });

    IT("keeps folder identities stable and returns a deleted active folder to Favorites", {
        KCDC::Features::Prop::FavoriteFolders favorites;
        std::string error;
        const auto first  = favorites.Add("Buildings", error);
        const auto second = favorites.Add("Nature", error);
        EQUALS(first > KCDC::Features::Prop::FavoriteFolders::kRoot, true);
        EQUALS(second > first, true);
        EQUALS(favorites.Rename(first, "Town", error), true);
        EQUALS(favorites.Find(first)->name == "Town", true);
        EQUALS(favorites.Set("objects/unavailable/future.cgf", true, first, error), true);
        EQUALS(favorites.SetActive(first, error), true);
        EQUALS(favorites.Delete(first, error), true);
        EQUALS(favorites.Find(first) == nullptr, true);
        EQUALS(favorites.ActiveId(), KCDC::Features::Prop::FavoriteFolders::kRoot);
        EQUALS(favorites.Find(second)->name == "Nature", true);
        EQUALS(favorites.Add("Replacement", error) > second, true);
        EQUALS(favorites.Delete(KCDC::Features::Prop::FavoriteFolders::kRoot, error), false);
        EQUALS(favorites.Rename(KCDC::Features::Prop::FavoriteFolders::kRoot, "Other", error), false);
    });

    IT("moves favorites atomically and deduplicates an existing destination entry", {
        KCDC::Features::Prop::FavoriteFolders favorites;
        std::string error;
        const auto from        = favorites.Add("From", error);
        const auto to          = favorites.Add("To", error);
        const std::string path = "objects/test/tree.cgf";
        EQUALS(favorites.Set(path, true, from, error), true);
        EQUALS(favorites.Move(path, from, from, error), true);
        EQUALS(favorites.Contains(path, from), true);
        const auto before = favorites.ToJson();
        EQUALS(favorites.Move(path, from, 999, error), false);
        EQUALS(favorites.ToJson() == before, true);
        EQUALS(favorites.Set(path, true, to, error), true);
        EQUALS(favorites.Move(path, from, to, error), true);
        EQUALS(favorites.Contains(path, from), false);
        EQUALS(favorites.Find(to)->paths.size(), std::size_t(1));
        EQUALS(favorites.Move(path, from, to, error), false);
        EQUALS(favorites.Set(path, false, to, error), true);
        EQUALS(favorites.Find(to)->paths.empty(), true);
    });

    IT("rejects a full destination before removing the source favorite", {
        KCDC::Features::Prop::FavoriteFolders favorites;
        std::string error;
        const auto destination = favorites.Add("Full", error);
        for (std::size_t index = 0; index < KCDC::Features::Prop::FavoriteFolders::kMaxPaths; ++index) {
            EQUALS(favorites.Set("objects/test/" + std::to_string(index), true, destination, error), true);
        }
        const std::string path = "objects/test/move.cgf";
        EQUALS(favorites.Set(path, true, KCDC::Features::Prop::FavoriteFolders::kRoot, error), true);
        EQUALS(favorites.Move(path, KCDC::Features::Prop::FavoriteFolders::kRoot, destination, error), false);
        EQUALS(favorites.Contains(path, KCDC::Features::Prop::FavoriteFolders::kRoot), true);
        EQUALS(favorites.Contains(path, destination), false);
        EQUALS(favorites.Set("objects/test/0", true, destination, error), true);
    });

    IT("round trips unavailable catalog assets and rejects malformed favorites without replacing state", {
        KCDC::Features::Prop::FavoriteFolders favorites;
        std::string error;
        const auto folder = favorites.Add("Future assets", error);
        EQUALS(favorites.Set("objects/not-in-current-catalog.cgf", true, folder, error), true);
        const auto saved = favorites.ToJson();
        KCDC::Features::Prop::FavoriteFolders restored;
        EQUALS(KCDC::Features::Prop::FavoriteFolders::FromJson(saved, restored, error), true);
        EQUALS(restored == favorites, true);
        std::vector<nlohmann::json> invalid;
        auto malformed      = saved;
        malformed["active"] = 999;
        invalid.push_back(malformed);
        malformed                     = saved;
        malformed["folders"][1]["id"] = 1;
        invalid.push_back(malformed);
        malformed                       = saved;
        malformed["folders"][1]["name"] = "Favorites";
        invalid.push_back(malformed);
        malformed = saved;
        malformed["folders"][1]["paths"].push_back("objects/not-in-current-catalog.cgf");
        invalid.push_back(malformed);
        malformed                       = saved;
        malformed["folders"][0]["name"] = "Renamed root";
        invalid.push_back(malformed);
        malformed = saved;
        malformed["folders"].erase(malformed["folders"].begin());
        invalid.push_back(malformed);
        malformed           = saved;
        malformed["nextId"] = folder;
        invalid.push_back(malformed);
        for (const auto &id : std::vector<nlohmann::json> {0, -1, 1.5, "1", std::uint64_t(1) << 32}) {
            malformed           = saved;
            malformed["active"] = id;
            invalid.push_back(malformed);
        }
        for (const auto &bad : invalid) {
            EQUALS(KCDC::Features::Prop::FavoriteFolders::FromJson(bad, restored, error), false);
            EQUALS(error.empty(), false);
            EQUALS(restored == favorites, true);
        }
    });

    IT("keeps persisted favorite IDs above the signed integer range addressable", {
        KCDC::Features::Prop::FavoriteFolders favorites;
        auto saved                       = favorites.ToJson();
        constexpr std::uint32_t sourceId = 2147483648U;
        constexpr std::uint32_t targetId = std::numeric_limits<std::uint32_t>::max() - 1;
        saved["folders"].push_back({{"id", sourceId}, {"name", "Source"}, {"paths", {"objects/test/high-id.cgf"}}});
        saved["folders"].push_back({{"id", targetId}, {"name", "Target"}, {"paths", nlohmann::json::array()}});
        saved["nextId"] = std::numeric_limits<std::uint32_t>::max();
        saved["active"] = sourceId;
        std::string error;
        EQUALS(KCDC::Features::Prop::FavoriteFolders::FromJson(saved, favorites, error), true);
        EQUALS(favorites.SetActive(targetId, error), true);
        EQUALS(favorites.Move("objects/test/high-id.cgf", sourceId, targetId, error), true);
        EQUALS(favorites.Contains("objects/test/high-id.cgf", sourceId), false);
        EQUALS(favorites.Contains("objects/test/high-id.cgf", targetId), true);
        EQUALS(favorites.Rename(targetId, "Renamed target", error), true);
        EQUALS(favorites.Add("Exhausted", error), std::uint32_t(0));
        KCDC::Features::Prop::FavoriteFolders restored;
        EQUALS(KCDC::Features::Prop::FavoriteFolders::FromJson(favorites.ToJson(), restored, error), true);
        EQUALS(restored == favorites, true);
    });

    IT("validates folder names and asset paths before mutating project favorites", {
        KCDC::Features::Prop::FavoriteFolders favorites;
        std::string error;
        const auto folder = favorites.Add("Props", error);
        const auto before = favorites.ToJson();
        for (const auto &name : std::vector<std::string> {"", "Props", " leading", "trailing ", "line\nbreak", std::string(81, 'x')}) {
            EQUALS(favorites.Add(name, error), std::uint32_t(0));
            EQUALS(favorites.ToJson() == before, true);
        }
        for (const auto &path : std::vector<std::string> {"", std::string("a\0b", 3), "line\nbreak", std::string(1025, 'x')}) {
            EQUALS(favorites.Set(path, true, folder, error), false);
            EQUALS(favorites.ToJson() == before, true);
        }
        EQUALS(favorites.SetActive(999, error), false);
        EQUALS(favorites.ToJson() == before, true);
    });

    IT("defaults to a disabled grid linked to translation snap and supports an independent step", {
        KCDC::Features::Prop::EditorViewport viewport;
        EQUALS(viewport.localSpace, false);
        EQUALS(viewport.grid, false);
        EQUALS(viewport.gridMatchSnap, true);
        EQUALS(viewport.Grid(glm::vec3(1)).segments.empty(), true);
        viewport.snapTranslateStep = 2.0F;
        viewport.gridStep          = 0.25F;
        EQUALS(viewport.GridStep(), 2.0F);
        viewport.gridMatchSnap = false;
        EQUALS(viewport.GridStep(), 0.25F);
        viewport.grid = true;
        EQUALS(viewport.Grid(glm::vec3(1)).segments.empty(), false);
    });

    IT("persists grid line width and supplies a default for older viewport settings", {
        KCDC::Features::Prop::EditorViewport viewport;
        viewport.grid          = true;
        viewport.gridLineWidth = 4.0F;
        std::string error;
        KCDC::Features::Prop::EditorViewport restored;
        EQUALS(KCDC::Features::Prop::EditorViewport::FromJson(viewport.ToJson(), restored, error), true);
        EQUALS(restored.Grid({0, 0, 0}).width, 4.0F);
        const auto previous = restored.ToJson();
        for (const float invalid : {0.0F, 8.1F, std::numeric_limits<float>::infinity()}) {
            auto settings             = previous;
            settings["gridLineWidth"] = invalid;
            EQUALS(KCDC::Features::Prop::EditorViewport::FromJson(settings, restored, error), false);
            EQUALS(restored.ToJson() == previous, true);
        }
        auto legacy = previous;
        legacy.erase("gridLineWidth");
        EQUALS(KCDC::Features::Prop::EditorViewport::FromJson(legacy, restored, error), true);
        EQUALS(restored.Grid({0, 0, 0}).width, 1.5F);
    });

    IT("keeps the world grid stationary within negative camera cells and follows whole cell steps", {
        KCDC::Features::Prop::EditorViewport viewport;
        viewport.grid              = true;
        viewport.snapTranslateStep = 0.5F;
        viewport.gridHeightOffset  = -2.0F;
        const auto first           = viewport.Grid({-0.1F, -1.1F, 4.1F});
        const auto sameCell        = viewport.Grid({-0.4F, -1.4F, 4.4F});
        const auto crossed         = viewport.Grid({0.1F, -1.1F, 4.1F});
        EQUALS(glm::length(first.boundsCentre - glm::vec3(-0.5F, -1.5F, 2.0F)) < 0.0001F, true);
        EQUALS(first.segments.size(), sameCell.segments.size());
        EQUALS(first.segments.size(), crossed.segments.size());
        for (std::size_t index = 0; index < first.segments.size(); ++index) {
            EQUALS(glm::length(first.segments[index].from - sameCell.segments[index].from) < 0.0001F, true);
            EQUALS(glm::length(first.segments[index].to - sameCell.segments[index].to) < 0.0001F, true);
            EQUALS(glm::length(crossed.segments[index].from - first.segments[index].from - glm::vec3(0.5F, 0, 0)) < 0.0001F, true);
            for (const auto &point : {first.segments[index].from, first.segments[index].to}) {
                EQUALS(std::abs(point.x / 0.5F - std::round(point.x / 0.5F)) < 0.0001F, true);
                EQUALS(std::abs(point.y / 0.5F - std::round(point.y / 0.5F)) < 0.0001F, true);
                EQUALS(point.z, 2.0F);
            }
        }
    });

    IT("bounds grid geometry independently of spacing and rejects invalid viewport input atomically", {
        KCDC::Features::Prop::EditorViewport viewport;
        viewport.grid          = true;
        viewport.gridMatchSnap = false;
        for (const float step : {0.01F, 0.5F, 50.0F}) {
            viewport.gridStep  = step;
            const auto drawing = viewport.Grid({120, -60, 15});
            EQUALS(drawing.segments.size(), std::size_t(130));
            EQUALS(drawing.triangles.empty(), true);
            EQUALS(drawing.depthTest, true);
            EQUALS((std::isfinite(drawing.boundsRadius) && drawing.boundsRadius > 0.0F), true);
            for (const auto &edge : drawing.segments) {
                EQUALS(glm::length(edge.from - drawing.boundsCentre) <= drawing.boundsRadius + 0.01F, true);
                EQUALS(glm::length(edge.to - drawing.boundsCentre) <= drawing.boundsRadius + 0.01F, true);
            }
        }
        const auto before = viewport.ToJson();
        std::string error;
        for (const auto &patch : std::vector<nlohmann::json> {{{"space", "screen"}}, {{"grid", "yes"}}, {{"gridStep", 0}}, {{"snapTranslateStep", -1}}, {{"snapRotateStep", 91}}, {{"snapScaleStep", 0}}, {{"gridHeightOffset", 101}}, {{"gridStep", nullptr}}, {{"gridStep", "1"}}}) {
            auto malformed = before;
            malformed.update(patch);
            EQUALS(KCDC::Features::Prop::EditorViewport::FromJson(malformed, viewport, error), false);
            EQUALS(viewport.ToJson() == before, true);
        }
        viewport.gridStep = std::numeric_limits<float>::quiet_NaN();
        EQUALS(viewport.IsValid(), false);
        EQUALS(viewport.Grid(glm::vec3(0)).segments.empty(), true);
    });

    IT("round trips per-project prop and effect folders with viewport preferences", {
        KCDC::Features::Prop::MapDocument document;
        document.level                      = "kutnohorsko";
        document.viewport.localSpace        = true;
        document.viewport.grid              = true;
        document.viewport.gridMatchSnap     = false;
        document.viewport.gridStep          = 2.5F;
        document.viewport.snapTranslate     = true;
        document.viewport.snapTranslateStep = 0.25F;
        document.viewport.gridHeightOffset  = -4.0F;
        document.viewport.gridLineWidth     = 4.0F;
        std::string error;
        const auto props   = document.propFavorites.Add("Town", error);
        const auto effects = document.effectFavorites.Add("Lights", error);
        EQUALS(document.propFavorites.Set("objects/absent/custom.cgf", true, props, error), true);
        EQUALS(document.effectFavorites.Set("custom.effects.fire", true, effects, error), true);
        KCDC::Features::Prop::MapDocument restored;
        EQUALS(KCDC::Features::Prop::ParseProject(KCDC::Features::Prop::SerializeProject(document), restored, error), true);
        EQUALS(restored.propFavorites == document.propFavorites, true);
        EQUALS(restored.effectFavorites == document.effectFavorites, true);
        EQUALS(restored.viewport.ToJson() == document.viewport.ToJson(), true);
        EQUALS(KCDC::Features::Prop::ProjectToJson(restored) == KCDC::Features::Prop::ProjectToJson(document), true);
    });

    IT("loads older projects with fresh editor defaults even after another project was open", {
        KCDC::Features::Prop::MapDocument previous;
        previous.level               = "kutnohorsko";
        previous.viewport.grid       = true;
        previous.viewport.localSpace = true;
        std::string error;
        previous.propFavorites.Add("Old project", error);
        previous.effectFavorites.Add("Old effects", error);
        auto project = KCDC::Features::Prop::ProjectToJson(previous);
        project["editor"].erase("viewport");
        project["editor"].erase("favorites");
        project["editor"].erase("layers");
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), previous, error), true);
        const KCDC::Features::Prop::MapDocument defaults;
        EQUALS(previous.viewport.ToJson() == defaults.viewport.ToJson(), true);
        EQUALS(previous.propFavorites == defaults.propFavorites, true);
        EQUALS(previous.effectFavorites == defaults.effectFavorites, true);
    });

    IT("rejects malformed persisted editor preferences rather than silently accepting them", {
        KCDC::Features::Prop::MapDocument document;
        document.level   = "kutnohorsko";
        const auto valid = KCDC::Features::Prop::ProjectToJson(document);
        std::vector<nlohmann::json> invalid;
        auto malformed                           = valid;
        malformed["editor"]["viewport"]["space"] = "view";
        invalid.push_back(malformed);
        malformed = valid;
        malformed["editor"]["favorites"].erase("effects");
        invalid.push_back(malformed);
        malformed                                           = valid;
        malformed["editor"]["favorites"]["props"]["active"] = 42;
        invalid.push_back(malformed);
        malformed                                = valid;
        malformed["editor"]["layers"]["objects"] = {"default"};
        invalid.push_back(malformed);
        for (const auto &bad : invalid) {
            std::string error;
            EQUALS(KCDC::Features::Prop::ParseProject(bad.dump(), document, error), false);
            EQUALS(error.empty(), false);
        }
    });

    IT("rejects brush layer aliases using the same identity as world document application", {
        KCDC::Features::Prop::MapDocument document;
        std::string error;
        const auto custom = document.layers.Create("Buildings", error);
        KCDC::Shared::Game::LevelEditDefinition brush;
        brush.name = "objects/test/brush.cgf";
        brush.at   = {-0.01F, 1.0F, 2.0F};
        document.worldLayers.push_back({brush, custom});
        const auto saved  = KCDC::Features::Prop::ProjectToJson(document);
        const auto before = KCDC::Features::Prop::ProjectToJson(document);
        std::vector<nlohmann::json> aliases;
        auto alias            = saved["editor"]["layers"]["world"][0];
        alias["layer"]        = "default";
        alias["object"]["at"] = {{"x", -0.01}, {"y", 1}, {"z", 2}};
        aliases.push_back(alias);
        alias["object"]["at"]   = {-0.01, 1, 2};
        alias["object"]["name"] = "OBJECTS/TEST/BRUSH.CGF";
        aliases.push_back(alias);
        alias["object"]["at"] = {0.01, 1, 2}; // same brush across a spatial-cell boundary
        aliases.push_back(alias);
        for (const auto &member : aliases) {
            auto malformed = saved;
            malformed["editor"]["layers"]["world"].push_back(member);
            EQUALS(KCDC::Features::Prop::ParseProject(malformed.dump(), document, error), false);
            EQUALS(KCDC::Features::Prop::ProjectToJson(document) == before, true);
        }
        auto distinct         = saved;
        alias["object"]["at"] = {0.06, 1, 2};
        distinct["editor"]["layers"]["world"].push_back(alias);
        EQUALS(KCDC::Features::Prop::ParseProject(distinct.dump(), document, error), true);
        EQUALS(document.worldLayers.size(), std::size_t(2));
    });

    IT("keys entity layer members by GUID independently of position or brush names", {
        KCDC::Features::Prop::MapDocument document;
        KCDC::Shared::Game::LevelEditDefinition entity;
        entity.kind = KCDC::Shared::Game::LevelEditKind::Entity;
        entity.guid = 1;
        entity.name = "objects/test/brush.cgf";
        document.worldLayers.push_back({entity, "default"});
        auto project          = KCDC::Features::Prop::ProjectToJson(document);
        auto alias            = project["editor"]["layers"]["world"][0];
        alias["object"]["at"] = {100, 200, 300};
        project["editor"]["layers"]["world"].push_back(alias);
        std::string error;
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), document, error), false);
        alias["object"]["guid"]                 = "0000000000000002";
        alias["object"]["at"]                   = {0, 0, 0};
        project["editor"]["layers"]["world"][1] = alias;
        alias["object"]["kind"]                 = "brush";
        project["editor"]["layers"]["world"].push_back(alias);
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), document, error), true);
        EQUALS(document.worldLayers.size(), std::size_t(3));
    });

    IT("bounds aggregate custom project membership across props effects and world objects", {
        KCDC::Features::Prop::MapDocument source;
        std::string error;
        const auto custom = source.layers.Create("Buildings", error);
        KCDC::Features::Prop::DocumentObject object;
        object.model = "objects/test/brush.cgf";
        object.layer = custom;
        source.objects.resize(KCDC::Features::Prop::EditorLayers::kMaxMembers, object);
        KCDC::Features::Prop::DocumentEffect effect;
        effect.effect = "test.effect";
        source.effects.push_back(effect); // Default membership does not consume the custom limit.
        auto project = KCDC::Features::Prop::ProjectToJson(source);
        KCDC::Features::Prop::MapDocument destination;
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), destination, error), true);
        EQUALS(destination.objects.size(), KCDC::Features::Prop::EditorLayers::kMaxMembers);
        destination                               = {};
        const auto before                         = KCDC::Features::Prop::ProjectToJson(destination);
        project["editor"]["layers"]["effects"][0] = custom;
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), destination, error), false);
        EQUALS(error, std::string("Too many layer members."));
        EQUALS(KCDC::Features::Prop::ProjectToJson(destination) == before, true);
        project["editor"]["layers"]["effects"][0] = "default";
        KCDC::Shared::Game::LevelEditDefinition world;
        world.kind = KCDC::Shared::Game::LevelEditKind::Entity;
        world.guid = 1;
        project["editor"]["layers"]["world"].push_back({{"object", KCDC::Shared::Game::LevelEditDefinitionToJson(world)}, {"layer", custom}});
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), destination, error), false);
        EQUALS(error, std::string("Too many layer members."));
        EQUALS(KCDC::Features::Prop::ProjectToJson(destination) == before, true);
    });

    IT("keeps layers favorites and viewport preferences out of runtime map exports", {
        KCDC::Features::Prop::MapDocument document;
        document.level = "kutnohorsko";
        document.objects.push_back({"objects/test/prop.cgf"});
        KCDC::Features::Prop::DocumentEffect effect;
        effect.effect = "test.effect";
        document.effects.push_back(effect);
        const auto before = KCDC::Features::Prop::MapDocumentToJson(document);
        std::string error;
        const auto layer = document.layers.Create("Editor only", error);
        EQUALS(document.layers.SetVisible(layer, false, error), true);
        EQUALS(document.layers.SetLocked(layer, true, error), true);
        document.objects.front().layer = layer;
        document.effects.front().layer = layer;
        document.propFavorites.Add("Private favorites", error);
        document.effectFavorites.Add("Private effects", error);
        document.viewport.localSpace = true;
        document.viewport.grid       = true;
        EQUALS(KCDC::Features::Prop::MapDocumentToJson(document) == before, true);
        EQUALS(KCDC::Features::Prop::ProjectToJson(document)["scene"] == before, true);
    });

    IT("persists preview conditions and leaves older projects under the level's own", {
        KCDC::Features::Prop::MapDocument document, restored;
        document.level                     = "kutnohorsko";
        document.environment.pinSky        = true;
        document.environment.skyHour       = 5.5F;
        document.environment.clockSpeed    = 0.0F;
        document.environment.preset        = "overcast_light_rain";
        document.environment.blendSeconds  = 120;
        document.environment.holdWeather   = true;
        document.environment.overrideRain  = true;
        document.environment.rainIntensity = 0.75F;
        std::string error;
        auto project = KCDC::Features::Prop::ProjectToJson(document);
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), true);
        EQUALS(restored.environment == document.environment, true);

        // An older project has no environment block at all, and opens under
        // whatever the level chose for itself.
        project["editor"].erase("environment");
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), true);
        EQUALS(restored.environment.IsDefault(), true);
        EQUALS(restored.environment.clockSpeed, KCDC::Features::Prop::EnvironmentPreview::kDefaultClockSpeed);

        // A malformed block fails the whole file rather than being dropped,
        // and never half-applies.
        const auto valid                            = document.environment;
        project["editor"]["environment"]            = valid.ToJson();
        project["editor"]["environment"]["skyHour"] = 25.0F;
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), false);
        project["editor"]["environment"]           = valid.ToJson();
        project["editor"]["environment"]["preset"] = std::string(200, 'x');
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), false);
        auto rejected                                   = valid.ToJson();
        rejected["clockSpeed"]                          = -1.0F;
        KCDC::Features::Prop::EnvironmentPreview target = valid;
        EQUALS(KCDC::Features::Prop::EnvironmentPreview::FromJson(rejected, target, error), false);
        EQUALS(target == valid, true);
    });

    IT("keeps preview conditions out of the scene, and says what the engine will not do as asked", {
        KCDC::Features::Prop::MapDocument document;
        document.level                   = "kutnohorsko";
        const auto scene                 = KCDC::Features::Prop::MapDocumentToJson(document);
        document.environment.pinSky      = true;
        document.environment.holdWeather = true;
        // Preview conditions are not authored content: no export carries them.
        EQUALS(KCDC::Features::Prop::MapDocumentToJson(document) == scene, true);
        EQUALS(KCDC::Features::Prop::ProjectToJson(document)["scene"] == scene, true);

        KCDC::Features::Prop::EnvironmentPreview preview;
        EQUALS(preview.Warnings().empty(), true);
        // A held sky suspends the weather system, which also stops rain.
        preview.holdWeather  = true;
        preview.overrideRain = true;
        preview.blendSeconds = 300;
        EQUALS(preview.Warnings().size(), std::size_t(2));
        EQUALS(preview.EffectiveBlendSeconds(), 0);
        // Left running, the weather system blends a chosen preset away again.
        preview.holdWeather  = false;
        preview.overrideRain = false;
        preview.preset       = "clear_sky";
        preview.clockSpeed   = 0.0F;
        EQUALS(preview.Warnings().size(), std::size_t(2));
        EQUALS(preview.EffectiveBlendSeconds(), 300);
    });

    IT("accepts every field of the published environment schema and refuses what the parser refuses", {
        const auto schema = KCDC::Features::Prop::EnvironmentPreview::Schema();
        KCDC::Features::Prop::EnvironmentPreview preview;
        preview.pinSky        = true;
        preview.skyHour       = 24.0F;
        preview.clockSpeed    = KCDC::Features::Prop::EnvironmentPreview::kMaxClockSpeed;
        preview.preset        = "storm";
        preview.blendSeconds  = KCDC::Features::Prop::EnvironmentPreview::kMaxBlendSeconds;
        preview.holdWeather   = true;
        preview.overrideRain  = true;
        preview.rainIntensity = 1.0F;
        std::string error;
        EQUALS(KCDC::Shared::WorldBuilder::McpServer::ValidateArguments(schema, preview.ToJson(), error), true);
        // An empty patch is valid: the tool merges it over what is already set.
        EQUALS(KCDC::Shared::WorldBuilder::McpServer::ValidateArguments(schema, nlohmann::json::object(), error), true);
        // Named, because items() over a temporary json outlives nothing.
        const nlohmann::json refused {{"skyHour", 24.5F}, {"clockSpeed", 500.0F}, {"rainIntensity", 1.5F}, {"blendSeconds", 7200}, {"pinSky", "yes"}, {"unknown", 1}};
        for (const auto &[key, value] : refused.items()) {
            auto patch = nlohmann::json::object();
            patch[key] = value;
            EQUALS(KCDC::Shared::WorldBuilder::McpServer::ValidateArguments(schema, patch, error), false);
        }
    });
});
#endif
