#pragma once

#include "features/prop/prop_editor_document.h"
#include "features/prop/prop_editor_history.h"
#include "shared/features/world_builder/world_document.h"

#include <limits>

MODULE(world_builder_lights, {
    IT("roundtrips authored lights, groups and independent layer membership", {
        KCDC::Features::Prop::MapDocument document;
        document.level = "kutnohorsko";
        std::string error;
        KCDC::Features::Prop::DocumentLight light;
        light.position           = {12, 34, 56};
        light.tuning.color       = {0.2F, 0.4F, 0.8F};
        light.tuning.intensity   = 3.0F;
        light.tuning.radius      = 8.0F;
        light.tuning.castShadows = true;
        light.group              = "torch";
        light.layer              = document.layers.Create("Lighting", error);
        EQUALS(document.layers.SetVisible(light.layer, false, error), true);
        document.lights.push_back(light);
        KCDC::Features::Prop::MapDocument restored;
        EQUALS(KCDC::Features::Prop::ParseProject(KCDC::Features::Prop::SerializeProject(document), restored, error), true);
        EQUALS(restored.lights.size(), std::size_t(1));
        EQUALS(restored.lights.front().position.x, 12.0F);
        EQUALS(restored.lights.front().tuning == light.tuning, true);
        EQUALS(restored.lights.front().group, light.group);
        EQUALS(restored.lights.front().layer, light.layer);
        EQUALS(restored.layers.Find(light.layer)->visible, false);
        EQUALS(KCDC::Features::Prop::ProjectToJson(restored) == KCDC::Features::Prop::ProjectToJson(document), true);
    });

    IT("loads existing project and world versions without light arrays", {
        KCDC::Features::Prop::MapDocument document;
        document.level = "kutnohorsko";
        auto project   = KCDC::Features::Prop::ProjectToJson(document);
        project["scene"].erase("lights");
        project["editor"]["layers"].erase("lights");
        std::string error;
        for (int version : {1, 2}) {
            project["version"]          = version;
            project["scene"]["version"] = version;
            KCDC::Features::Prop::MapDocument restored;
            EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), true);
            EQUALS(restored.lights.empty(), true);
            auto resource = KCDC::Shared::WorldBuilder::ExportResource(project["scene"]);
            resource.erase("lights");
            resource["version"] = version;
            nlohmann::json loaded;
            EQUALS(KCDC::Shared::WorldBuilder::ReadResource(resource.dump(), loaded, error), true);
        }
    });

    IT("exports light runtime controls and valid script calls without editor metadata", {
        KCDC::Features::Prop::MapDocument document;
        document.level = "kutnohorsko";
        KCDC::Features::Prop::DocumentLight light;
        light.position           = {1, 2, 3};
        light.group              = "torch";
        light.tuning.castShadows = true;
        light.layer              = "lighting";
        document.lights.push_back(light);
        auto map                  = KCDC::Features::Prop::MapDocumentToJson(document);
        map["lights"][0]["layer"] = "lighting";
        const auto resource       = KCDC::Shared::WorldBuilder::ExportResource(map);
        const auto &entry         = resource["lights"][0];
        EQUALS(entry.contains("group"), false);
        EQUALS(entry.contains("layer"), false);
        EQUALS(entry.at("position") == nlohmann::json::array({1, 2, 3}), true);
        EQUALS(entry.at("radius").get<float>(), light.tuning.radius);
        EQUALS(entry.at("castShadows").get<bool>(), true);
        std::string error;
        EQUALS(KCDC::Shared::WorldBuilder::ValidateContent(resource, error), true);
        const auto script   = KCDC::Shared::WorldBuilder::ApiCalls(resource);
        const auto options  = nlohmann::json {{"color", {{"x", light.tuning.color.x}, {"y", light.tuning.color.y}, {"z", light.tuning.color.z}}}, {"intensity", light.tuning.intensity}, {"radius", light.tuning.radius}, {"castShadows", true}};
        const auto position = nlohmann::json {{"x", 1.0F}, {"y", 2.0F}, {"z", 3.0F}};
        EQUALS(script, "Light.spawn(" + position.dump() + ", " + options.dump() + ");\n");
    });

    IT("defaults old light documents to unshadowed and rejects nonboolean shadow controls atomically", {
        KCDC::Features::Prop::MapDocument document;
        document.lights.push_back({});
        auto project = KCDC::Features::Prop::ProjectToJson(document);
        project["scene"]["lights"][0].erase("castShadows");
        std::string error;
        KCDC::Features::Prop::MapDocument restored;
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), true);
        EQUALS(restored.lights.front().tuning.castShadows, false);
        KCDC::Shared::Game::LightTuning tuning;
        tuning.castShadows = true;
        EQUALS(KCDC::Shared::WorldBuilder::ReadLightTuning(project["scene"]["lights"][0], tuning), true);
        EQUALS(tuning.castShadows, false);
        auto resource = KCDC::Shared::WorldBuilder::ExportResource(project["scene"]);
        EQUALS(KCDC::Shared::WorldBuilder::ApiCalls(resource).find("\"castShadows\":false") != std::string::npos, true);
        for (const auto &invalid : {nlohmann::json(nullptr), nlohmann::json(0), nlohmann::json(1), nlohmann::json("true"), nlohmann::json::array(), nlohmann::json::object()}) {
            project["scene"]["lights"][0]["castShadows"] = invalid;
            restored.level                               = "preserved";
            EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), false);
            EQUALS(restored.level, std::string("preserved"));
            tuning.castShadows = true;
            EQUALS(KCDC::Shared::WorldBuilder::ReadLightTuning(project["scene"]["lights"][0], tuning), false);
            EQUALS(tuning.castShadows, true);
        }
    });

    IT("rejects invalid light inputs before replacing an existing project", {
        KCDC::Features::Prop::MapDocument document;
        document.lights.push_back({});
        const auto valid = KCDC::Features::Prop::ProjectToJson(document);
        std::string error;
        for (const auto &bad : std::vector<nlohmann::json> {
                 {{"position", {16385, 0, 0}}, {"color", {1, 1, 1}}, {"intensity", 1}, {"radius", 5}},
                 {{"position", {-16385, 0, 0}}, {"color", {1, 1, 1}}, {"intensity", 1}, {"radius", 5}},
                 {{"position", {0, 0}}, {"color", {1, 1, 1}}, {"intensity", 1}, {"radius", 5}},
                 {{"position", {0, 0, 0}}, {"color", {1.1, 1, 1}}, {"intensity", 1}, {"radius", 5}},
                 {{"position", {0, 0, 0}}, {"color", {-0.1, 1, 1}}, {"intensity", 1}, {"radius", 5}},
                 {{"position", {0, 0, 0}}, {"color", {1, 1, 1}}, {"intensity", -1}, {"radius", 5}},
                 {{"position", {0, 0, 0}}, {"color", {1, 1, 1}}, {"intensity", 101}, {"radius", 5}},
                 {{"position", {0, 0, 0}}, {"color", {1, 1, 1}}, {"intensity", 1}, {"radius", 0}},
                 {{"position", {0, 0, 0}}, {"color", {1, 1, 1}}, {"intensity", 1}, {"radius", 101}},
                 {{"position", {0, 0, 0}}, {"color", {1, 1, 1}}, {"intensity", "1"}, {"radius", 5}},
                 {{"position", {0, 0, 0}}, {"color", {1, 1, 1}}, {"intensity", 1}},
             }) {
            auto project                  = valid;
            project["scene"]["lights"][0] = bad;
            KCDC::Features::Prop::MapDocument destination;
            destination.level = "preserved";
            EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), destination, error), false);
            EQUALS(destination.level, std::string("preserved"));
            EQUALS(error.find("lights[0]") != std::string::npos, true);
        }
        for (const auto &bad : {nlohmann::json::object(), nlohmann::json(nullptr), nlohmann::json(1)}) {
            auto project               = valid;
            project["scene"]["lights"] = bad;
            EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), document, error), false);
        }
        auto project                          = valid;
        project["editor"]["layers"]["lights"] = {"missing"};
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), document, error), false);
        project = valid;
        project["editor"]["layers"].erase("lights");
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), document, error), false);
        KCDC::Shared::Game::LightTuning tuning;
        tuning.intensity = std::numeric_limits<float>::infinity();
        EQUALS(tuning.IsSane(), false);
        tuning.intensity = 1.0F;
        tuning.color.x   = std::numeric_limits<float>::quiet_NaN();
        EQUALS(tuning.IsSane(), false);
    });

    IT("preserves valid lights while reporting invalid legacy map entries", {
        KCDC::Features::Prop::MapDocument document;
        document.lights.push_back({});
        auto map      = KCDC::Features::Prop::MapDocumentToJson(document);
        auto bad      = map["lights"][0];
        bad["radius"] = -1;
        map["lights"].push_back(bad);
        std::size_t skipped = 0;
        std::string error;
        EQUALS(KCDC::Features::Prop::MapDocumentFromJson(map, document, skipped, error), true);
        EQUALS(skipped, std::size_t(1));
        EQUALS(document.lights.size(), std::size_t(1));
    });

    IT("anchors light-only blueprints and offsets every attached light", {
        KCDC::Features::Prop::MapDocument document;
        KCDC::Features::Prop::DocumentLight light;
        light.position           = {10, 20, 30};
        light.tuning.castShadows = true;
        document.lights.push_back(light);
        light.position = {11, 22, 33};
        document.lights.push_back(light);
        std::string text, error;
        EQUALS(KCDC::Features::Prop::SerializeBlueprint(document, text, error), true);
        KCDC::Features::Prop::MapDocument restored;
        EQUALS(KCDC::Features::Prop::ParseBlueprint(text, restored, error), true);
        EQUALS(restored.lights.size(), std::size_t(2));
        EQUALS(restored.lights[0].position.x, 0.0F);
        EQUALS(restored.lights[0].position.y, 0.0F);
        EQUALS(restored.lights[0].position.z, 0.0F);
        KCDC::Features::Prop::OffsetDocument(restored, {100, 200, 300});
        EQUALS(restored.lights[1].position.x, 101.0F);
        EQUALS(restored.lights[1].position.y, 202.0F);
        EQUALS(restored.lights[1].position.z, 303.0F);
        EQUALS(restored.lights[1].tuning == light.tuning, true);
    });

    IT("retains light layer assignments through placement and deletion history", {
        KCDC::Features::Prop::EditorLayers layers;
        KCDC::Features::Prop::EditorHistory history;
        std::string error;
        const auto layer = layers.Create("Lighting", error);
        EQUALS(layers.Assign({"light:12"}, layer, error), true);
        KCDC::Features::Prop::HistoryAction action;
        action.kind     = KCDC::Features::Prop::HistoryAction::Kind::LightPlace;
        action.light.id = 12;
        KCDC::Features::Prop::HistoryStep step;
        step.label = "Place light";
        step.actions.push_back(action);
        history.Record(step, &layers);
        EQUALS(history.UndoStep(&layers).has_value(), true);
        EQUALS(layers.Members().contains("light:12"), false);
        EQUALS(history.RedoStep(&layers).has_value(), true);
        EQUALS(layers.LayerOf("light:12"), layer);
        step.actions.front().kind = KCDC::Features::Prop::HistoryAction::Kind::LightDelete;
        history.Record(step, &layers);
        EQUALS(layers.Members().contains("light:12"), false);
        EQUALS(history.UndoStep(&layers).has_value(), true);
        EQUALS(layers.LayerOf("light:12"), layer);
        EQUALS(history.RedoStep(&layers).has_value(), true);
        EQUALS(layers.Members().contains("light:12"), false);
    });

    IT("folds light tuning gestures while retaining their original undo value", {
        KCDC::Features::Prop::EditorHistory history;
        KCDC::Features::Prop::HistoryStep step;
        step.label   = "Tune light";
        step.gesture = 7;
        KCDC::Features::Prop::HistoryAction action;
        action.kind                         = KCDC::Features::Prop::HistoryAction::Kind::LightTuning;
        action.id                           = 12;
        action.lightTuningBefore.intensity  = 1;
        action.lightTuningAfter.intensity   = 2;
        action.lightTuningAfter.castShadows = true;
        step.actions.push_back(action);
        history.Record(step);
        step.actions.front().lightTuningBefore.intensity   = 2;
        step.actions.front().lightTuningBefore.castShadows = true;
        step.actions.front().lightTuningAfter.intensity    = 3;
        history.Record(step);
        const auto undone = history.UndoStep();
        EQUALS(undone.has_value(), true);
        EQUALS(undone->actions.front().lightTuningBefore.intensity, 1.0F);
        EQUALS(undone->actions.front().lightTuningAfter.intensity, 3.0F);
        EQUALS(undone->actions.front().lightTuningBefore.castShadows, false);
        EQUALS(undone->actions.front().lightTuningAfter.castShadows, true);
        EQUALS(history.UndoStep().has_value(), false);
        const auto redone = history.RedoStep();
        EQUALS(redone->actions.front().lightTuningAfter.intensity, 3.0F);
        EQUALS(redone->actions.front().lightTuningAfter.castShadows, true);
        KCDC::Features::Prop::AuthoredHistoryState authored;
        authored.document.lights.push_back({});
        EQUALS(authored.IsValid(), false);
    });
});
