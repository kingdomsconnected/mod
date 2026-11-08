#pragma once

#include "prop_editor_areas.h"
#include "prop_editor_document.h"
#include "prop_editor_effects.h"
#include "prop_editor_lights.h"
#include "prop_editor_objects.h"
#include "prop_editor_scene.h"
#include "prop_editor_selection.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::Prop {
    /** A pose, as undo needs to put one back. */
    struct HistoryTransform {
        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
    };

    /** Immutable render-owned data carried through the game-thread history; native object IDs are never rebuilt. */
    struct AuthoredHistoryState {
        MapDocument document; // only areas, areaGroups, patrolRoutes and views
        std::vector<std::string> selectedAreas;
        std::vector<std::string> selectedPatrols;

        bool IsValid() const {
            if (!document.objects.empty() || !document.effects.empty() || !document.lights.empty() || !document.world.empty() || document.camera) {
                return false;
            }
            std::unordered_set<std::string> ids;
            std::string error;
            for (const auto &area : document.areas) {
                if (!EditorAreas::ValidateDefinition(area, error) || !ids.insert(area.id).second) {
                    return false;
                }
            }
            for (const auto &[id, group] : document.areaGroups) {
                if (!ids.contains(id) || group.empty()) {
                    return false;
                }
            }
            for (const auto &id : selectedAreas) {
                if (!ids.contains(id)) {
                    return false;
                }
            }
            ids.clear();
            for (const auto &route : document.patrolRoutes) {
                if (!route.IsSane() || !ids.insert(route.id).second) {
                    return false;
                }
            }
            if (selectedPatrols.size() > 1) {
                return false;
            }
            for (const auto &id : selectedPatrols) {
                if (!ids.contains(id)) {
                    return false;
                }
            }
            for (const auto &view : document.views) {
                if (!std::isfinite(view.position.x) || !std::isfinite(view.position.y) || !std::isfinite(view.position.z) || !std::isfinite(view.yawDegrees) || !std::isfinite(view.pitchDegrees)) {
                    return false;
                }
            }
            return true;
        }
    };

    /** One reversible change. Every field a kind does not use is left alone. */
    struct HistoryAction {
        enum class Kind : std::uint8_t {
            Layers,
            Group,
            EffectGroup,
            LightGroup,
            Place,          // `object` was put into the scene
            Delete,         // `object` was taken out of it
            Transform,      // placed object `id` moved from `before` to `after`
            Physics,        // placed object `id` was rebuilt with other physics
            Settings,       // placed door, smart object or item `id` was given other settings
            WorldTransform, // level object `id` moved
            WorldHide,      // level object `id` was hidden or shown
            WorldReplace,   // level object `id` was swapped for another mesh, or given back
            EffectPlace,    // `effect` was put into the scene
            EffectDelete,   // `effect` was taken out of it
            EffectMove,     // effect `id` moved
            EffectTuning,   // effect `id` was re-tuned
            LightPlace,
            LightDelete,
            LightMove,
            LightTuning,
            Authored, // render-owned areas, patrols and saved camera views
        };

        Kind kind        = Kind::Place;
        std::uint64_t id = 0;

        std::shared_ptr<const EditorLayers> layersBefore;
        std::shared_ptr<const EditorLayers> layersAfter;
        std::string layerId = "default"; // membership restored with a placement or deletion
        std::string groupBefore;
        std::string groupAfter;
        PlacedObject object;
        PlacedEffect effect;
        PlacedLight light;

        HistoryTransform before;
        HistoryTransform after;

        KCDC::Common::Spawn::PropPhysics physicsBefore = KCDC::Common::Spawn::PropPhysics::Static;
        KCDC::Common::Spawn::PropPhysics physicsAfter  = KCDC::Common::Spawn::PropPhysics::Static;

        nlohmann::json settingsBefore;
        nlohmann::json settingsAfter;

        bool hiddenBefore = false;
        bool hiddenAfter  = false;

        std::string replacementBefore;
        std::string replacementAfter;

        std::shared_ptr<const AuthoredHistoryState> authoredBefore;
        std::shared_ptr<const AuthoredHistoryState> authoredAfter;

        Shared::Game::VfxTuning tuningBefore;
        Shared::Game::VfxTuning tuningAfter;
        Shared::Game::LightTuning lightTuningBefore;
        Shared::Game::LightTuning lightTuningAfter;
    };

    /** What one undo takes back: a whole paste, or a whole drag. */
    struct HistoryStep {
        std::string label;

        /** Non-zero on every frame of one drag, which folds them into one step. */
        std::uint32_t gesture = 0;

        std::vector<HistoryAction> actions;
    };

    /**
     * Undo and redo for everything the editor changes. An action naming an
     * object that has since gone is skipped, so a stale step never blocks
     * the stack. Game thread only.
     */
    class EditorHistory final {
      public:
        static constexpr std::size_t kMaxSteps = 256;

        /** Records a step, folding it into the last one if they share a gesture. Clears redo. */
        void Record(HistoryStep step, EditorLayers *layers = nullptr) {
            if (layers) {
                for (auto &action : step.actions) {
                    const auto key = ObjectLayerKey(action);
                    if (!key.empty()) {
                        action.layerId = layers->LayerOf(key);
                    }
                }
            }
            if (step.actions.empty()) {
                return;
            }
            for (const auto &action : step.actions) {
                if (action.kind == HistoryAction::Kind::Authored && (!action.authoredBefore || !action.authoredAfter || !action.authoredBefore->IsValid() || !action.authoredAfter->IsValid())) {
                    return;
                }
            }
            if (layers) {
                for (const auto &action : step.actions) {
                    if (action.kind == HistoryAction::Kind::Delete || action.kind == HistoryAction::Kind::EffectDelete || action.kind == HistoryAction::Kind::LightDelete) {
                        layers->Forget(ObjectLayerKey(action));
                    }
                }
            }
            _redo.clear();
            if (step.gesture != 0 && !_undo.empty() && _undo.back().gesture == step.gesture && _undo.back().actions.size() == step.actions.size()) {
                auto &last = _undo.back();
                bool same  = true;
                for (std::size_t i = 0; i < step.actions.size(); ++i) {
                    same = same && last.actions[i].kind == step.actions[i].kind && last.actions[i].id == step.actions[i].id;
                }
                if (same) {
                    for (std::size_t i = 0; i < step.actions.size(); ++i) {
                        last.actions[i].after            = step.actions[i].after;
                        last.actions[i].lightTuningAfter = step.actions[i].lightTuningAfter;
                        last.actions[i].authoredAfter    = std::move(step.actions[i].authoredAfter);
                    }
                    return;
                }
            }
            _undo.push_back(std::move(step));
            if (_undo.size() > kMaxSteps) {
                _undo.erase(_undo.begin());
            }
        }

        /** Advances the timeline and editor-only layers; returned steps replay native object changes.
         * Membership follows the recorded lifetime even if a native restore fails;
         * a stale native action never blocks timeline advancement.
         */
        std::optional<HistoryStep> UndoStep(EditorLayers *layers = nullptr) {
            if (_undo.empty()) {
                return std::nullopt;
            }
            auto step = std::move(_undo.back());
            _undo.pop_back();
            for (auto it = step.actions.rbegin(); it != step.actions.rend(); ++it) {
                if (layers) {
                    if (it->kind == HistoryAction::Kind::Layers && it->layersBefore) {
                        layers->Restore(*it->layersBefore);
                    }
                    else if (it->kind == HistoryAction::Kind::Delete || it->kind == HistoryAction::Kind::EffectDelete || it->kind == HistoryAction::Kind::LightDelete) {
                        RestoreObjectLayer(*it, *layers);
                    }
                    else if (it->kind == HistoryAction::Kind::Place || it->kind == HistoryAction::Kind::EffectPlace || it->kind == HistoryAction::Kind::LightPlace) {
                        layers->Forget(ObjectLayerKey(*it));
                    }
                }
                if (it->kind == HistoryAction::Kind::Authored) {
                    _authoredRestore = it->authoredBefore;
                    ++_authoredRestoreSerial;
                }
            }
            _redo.push_back(step);
            return step;
        }
        std::optional<HistoryStep> RedoStep(EditorLayers *layers = nullptr) {
            if (_redo.empty()) {
                return std::nullopt;
            }
            auto step = std::move(_redo.back());
            _redo.pop_back();
            for (const auto &action : step.actions) {
                if (layers) {
                    if (action.kind == HistoryAction::Kind::Layers && action.layersAfter) {
                        layers->Restore(*action.layersAfter);
                    }
                    else if (action.kind == HistoryAction::Kind::Place || action.kind == HistoryAction::Kind::EffectPlace || action.kind == HistoryAction::Kind::LightPlace) {
                        RestoreObjectLayer(action, *layers);
                    }
                    else if (action.kind == HistoryAction::Kind::Delete || action.kind == HistoryAction::Kind::EffectDelete || action.kind == HistoryAction::Kind::LightDelete) {
                        layers->Forget(ObjectLayerKey(action));
                    }
                }
                if (action.kind == HistoryAction::Kind::Authored) {
                    _authoredRestore = action.authoredAfter;
                    ++_authoredRestoreSerial;
                }
            }
            _undo.push_back(step);
            return step;
        }
        const std::shared_ptr<const AuthoredHistoryState> &AuthoredRestore() const {
            return _authoredRestore;
        }
        std::uint64_t AuthoredRestoreSerial() const {
            return _authoredRestoreSerial;
        }

        /** False with nothing to undo. `label` says what was undone; `touched`, when given, the objects the step changed. */
        bool Undo(EditorScene &scene, WorldObjects &world, EditorEffects &effects, EditorLights &lights, std::string &label, EditorLayers *layers = nullptr, std::vector<SelectionRef> *touched = nullptr);

        bool Redo(EditorScene &scene, WorldObjects &world, EditorEffects &effects, EditorLights &lights, std::string &label, EditorLayers *layers = nullptr, std::vector<SelectionRef> *touched = nullptr);

        /** The objects one step changed, in the step's order, each once. Area, route and layer edits name none. */
        static std::vector<SelectionRef> TouchedObjects(const HistoryStep &step) {
            SelectionSet touched;
            for (const HistoryAction &action : step.actions) {
                switch (action.kind) {
                case HistoryAction::Kind::Place:
                case HistoryAction::Kind::Delete: touched.Insert(SelectionRef {SelectionKind::Placed, action.object.id != 0 ? action.object.id : action.id}); break;
                case HistoryAction::Kind::Transform:
                case HistoryAction::Kind::Physics:
                case HistoryAction::Kind::Group: touched.Insert(SelectionRef {SelectionKind::Placed, action.id}); break;
                case HistoryAction::Kind::WorldTransform:
                case HistoryAction::Kind::WorldHide:
                case HistoryAction::Kind::WorldReplace: touched.Insert(SelectionRef {SelectionKind::World, action.id}); break;
                case HistoryAction::Kind::EffectPlace:
                case HistoryAction::Kind::EffectDelete: touched.Insert(SelectionRef {SelectionKind::Effect, action.effect.id != 0 ? action.effect.id : action.id}); break;
                case HistoryAction::Kind::EffectMove:
                case HistoryAction::Kind::EffectTuning:
                case HistoryAction::Kind::EffectGroup: touched.Insert(SelectionRef {SelectionKind::Effect, action.id}); break;
                case HistoryAction::Kind::LightPlace:
                case HistoryAction::Kind::LightDelete: touched.Insert(SelectionRef {SelectionKind::Light, action.light.id != 0 ? action.light.id : action.id}); break;
                case HistoryAction::Kind::LightMove:
                case HistoryAction::Kind::LightTuning:
                case HistoryAction::Kind::LightGroup: touched.Insert(SelectionRef {SelectionKind::Light, action.id}); break;
                case HistoryAction::Kind::Layers:
                case HistoryAction::Kind::Authored: break;
                }
            }
            return touched.Items();
        }

        /** Adds every level object an undo or redo step names, which must stay tracked for it: moves, hides, and layer memberships a layer step restores. */
        void CollectWorldIds(std::unordered_set<std::uint64_t> &ids) const {
            const auto members = [&ids](const std::shared_ptr<const EditorLayers> &layers) {
                if (!layers) {
                    return;
                }
                for (const auto &[key, layer] : layers->Members()) {
                    std::uint64_t id = 0;
                    if (key.starts_with("world:") && std::from_chars(key.data() + 6, key.data() + key.size(), id).ec == std::errc {}) {
                        ids.insert(id);
                    }
                }
            };
            for (const auto *steps : {&_undo, &_redo}) {
                for (const HistoryStep &step : *steps) {
                    for (const HistoryAction &action : step.actions) {
                        if (action.kind == HistoryAction::Kind::WorldTransform || action.kind == HistoryAction::Kind::WorldHide || action.kind == HistoryAction::Kind::WorldReplace) {
                            ids.insert(action.id);
                        }
                        else if (action.kind == HistoryAction::Kind::Layers) {
                            members(action.layersBefore);
                            members(action.layersAfter);
                        }
                    }
                }
            }
        }

        void Clear() {
            _undo.clear();
            _redo.clear();
            _authoredRestore.reset();
            ++_authoredRestoreSerial;
        }

        /** What undo would take back, or empty. */
        const std::string &NextUndoLabel() const {
            return _undo.empty() ? _nothing : _undo.back().label;
        }
        const std::string &NextRedoLabel() const {
            return _redo.empty() ? _nothing : _redo.back().label;
        }

      private:
        static std::string ObjectLayerKey(const HistoryAction &action) {
            if (action.kind == HistoryAction::Kind::Place || action.kind == HistoryAction::Kind::Delete) {
                return "prop:" + std::to_string(action.object.id);
            }
            if (action.kind == HistoryAction::Kind::EffectPlace || action.kind == HistoryAction::Kind::EffectDelete) {
                return "effect:" + std::to_string(action.effect.id);
            }
            if (action.kind == HistoryAction::Kind::LightPlace || action.kind == HistoryAction::Kind::LightDelete) {
                return "light:" + std::to_string(action.light.id);
            }
            return {};
        }
        static void RestoreObjectLayer(const HistoryAction &action, EditorLayers &layers) {
            std::string error;
            layers.Assign({ObjectLayerKey(action)}, layers.Find(action.layerId) ? action.layerId : "default", error);
        }
        static void Revert(const HistoryAction &action, EditorScene &scene, WorldObjects &world, EditorEffects &effects, EditorLights &lights);
        static void Replay(const HistoryAction &action, EditorScene &scene, WorldObjects &world, EditorEffects &effects, EditorLights &lights);

        const std::string _nothing;
        std::shared_ptr<const AuthoredHistoryState> _authoredRestore;
        std::uint64_t _authoredRestoreSerial = 0;
        std::vector<HistoryStep> _undo;
        std::vector<HistoryStep> _redo;
    };
} // namespace KCDC::Features::Prop
