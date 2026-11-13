#include "prop_editor_history.h"

#include "core/obfuscation.h"

#include <utility>

namespace KCDC::Features::Prop {
    void EditorHistory::Revert(const HistoryAction &action, EditorScene &scene, WorldObjects &world, EditorEffects &effects, EditorLights &lights, EditorGrass &grass) {
        switch (action.kind) {
        case HistoryAction::Kind::Layers: break;   // restored by timeline advance
        case HistoryAction::Kind::Authored: break; // published by timeline advance and restored by the render owner
        case HistoryAction::Kind::Group: scene.SetGroup(action.id, action.groupBefore); break;
        case HistoryAction::Kind::EffectGroup: effects.SetGroup(action.id, action.groupBefore); break;
        case HistoryAction::Kind::Place: scene.Remove(action.object.id); break;
        case HistoryAction::Kind::Delete: scene.Restore(action.object); break;
        case HistoryAction::Kind::Transform: scene.SetTransform(action.id, action.before.position, action.before.rotation, action.before.scale); break;
        case HistoryAction::Kind::Physics: scene.SetPhysics(action.id, action.physicsBefore); break;
        case HistoryAction::Kind::Settings: {
            std::string ignored;
            scene.SetSettings(action.id, action.settingsBefore, ignored);
            break;
        }
        case HistoryAction::Kind::WorldTransform: world.SetTransform(action.id, action.before.position, action.before.rotation, action.before.scale); break;
        case HistoryAction::Kind::WorldHide: world.SetHidden(action.id, action.hiddenBefore); break;
        case HistoryAction::Kind::WorldReplace: world.SetReplacement(action.id, action.replacementBefore); break;
        case HistoryAction::Kind::EffectPlace: effects.Remove(action.effect.id); break;
        case HistoryAction::Kind::EffectDelete: effects.Restore(action.effect); break;
        case HistoryAction::Kind::EffectMove: effects.SetTransform(action.id, action.before.position, action.before.rotation); break;
        case HistoryAction::Kind::EffectTuning: effects.SetTuning(action.id, action.tuningBefore); break;
        case HistoryAction::Kind::LightGroup: lights.SetGroup(action.id, action.groupBefore); break;
        case HistoryAction::Kind::LightPlace: lights.Remove(action.light.id); break;
        case HistoryAction::Kind::LightDelete: lights.Restore(action.light); break;
        case HistoryAction::Kind::LightMove: lights.RestoreTransform(action.id, action.before.position); break;
        case HistoryAction::Kind::LightTuning: lights.RestoreTuning(action.id, action.lightTuningBefore); break;
        case HistoryAction::Kind::GrassPlace: grass.Remove(action.grass.id); break;
        case HistoryAction::Kind::GrassDelete: grass.Restore(action.grass); break;
        }
    }

    void EditorHistory::Replay(const HistoryAction &action, EditorScene &scene, WorldObjects &world, EditorEffects &effects, EditorLights &lights, EditorGrass &grass) {
        switch (action.kind) {
        case HistoryAction::Kind::Layers: break;   // restored by timeline advance
        case HistoryAction::Kind::Authored: break; // published by timeline advance and restored by the render owner
        case HistoryAction::Kind::Group: scene.SetGroup(action.id, action.groupAfter); break;
        case HistoryAction::Kind::EffectGroup: effects.SetGroup(action.id, action.groupAfter); break;
        case HistoryAction::Kind::Place: scene.Restore(action.object); break;
        case HistoryAction::Kind::Delete: scene.Remove(action.object.id); break;
        case HistoryAction::Kind::Transform: scene.SetTransform(action.id, action.after.position, action.after.rotation, action.after.scale); break;
        case HistoryAction::Kind::Physics: scene.SetPhysics(action.id, action.physicsAfter); break;
        case HistoryAction::Kind::Settings: {
            std::string ignored;
            scene.SetSettings(action.id, action.settingsAfter, ignored);
            break;
        }
        case HistoryAction::Kind::WorldTransform: world.SetTransform(action.id, action.after.position, action.after.rotation, action.after.scale); break;
        case HistoryAction::Kind::WorldHide: world.SetHidden(action.id, action.hiddenAfter); break;
        case HistoryAction::Kind::WorldReplace: world.SetReplacement(action.id, action.replacementAfter); break;
        case HistoryAction::Kind::EffectPlace: effects.Restore(action.effect); break;
        case HistoryAction::Kind::EffectDelete: effects.Remove(action.effect.id); break;
        case HistoryAction::Kind::EffectMove: effects.SetTransform(action.id, action.after.position, action.after.rotation); break;
        case HistoryAction::Kind::EffectTuning: effects.SetTuning(action.id, action.tuningAfter); break;
        case HistoryAction::Kind::LightGroup: lights.SetGroup(action.id, action.groupAfter); break;
        case HistoryAction::Kind::LightPlace: lights.Restore(action.light); break;
        case HistoryAction::Kind::LightDelete: lights.Remove(action.light.id); break;
        case HistoryAction::Kind::LightMove: lights.RestoreTransform(action.id, action.after.position); break;
        case HistoryAction::Kind::LightTuning: lights.RestoreTuning(action.id, action.lightTuningAfter); break;
        case HistoryAction::Kind::GrassPlace: grass.Restore(action.grass); break;
        case HistoryAction::Kind::GrassDelete: grass.Remove(action.grass.id); break;
        }
    }

    bool EditorHistory::Undo(EditorScene &scene, WorldObjects &world, EditorEffects &effects, EditorLights &lights, EditorGrass &grass, std::string &label, EditorLayers *layers, std::vector<SelectionRef> *touched) {
        KCDC_JUNK;
        if (!KCDC_OPAQUE_TRUE) {
            return false;
        }
        const auto step = UndoStep(layers);
        if (!step) {
            return false;
        }
        for (auto it = step->actions.rbegin(); it != step->actions.rend(); ++it) {
            Revert(*it, scene, world, effects, lights, grass);
        }
        label = step->label;
        if (touched != nullptr) {
            *touched = TouchedObjects(*step);
        }
        return true;
    }

    bool EditorHistory::Redo(EditorScene &scene, WorldObjects &world, EditorEffects &effects, EditorLights &lights, EditorGrass &grass, std::string &label, EditorLayers *layers, std::vector<SelectionRef> *touched) {
        KCDC_JUNK;
        if (!KCDC_OPAQUE_TRUE) {
            return false;
        }
        const auto step = RedoStep(layers);
        if (!step) {
            return false;
        }
        for (const auto &action : step->actions) {
            Replay(action, scene, world, effects, lights, grass);
        }
        label = step->label;
        if (touched != nullptr) {
            *touched = TouchedObjects(*step);
        }
        return true;
    }
} // namespace KCDC::Features::Prop
