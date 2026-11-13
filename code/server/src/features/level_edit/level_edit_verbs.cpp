#include "level_edit_verbs.h"
#include "level_edit_module.h"

#include "features/prop/prop_verbs.h"

#include "shared/entity_filter.h"
#include "shared/game/prop_models.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>
#include <utils/string_utils.h>

namespace KCDC::Features::LevelEdit {
    namespace {
        constexpr const char *kLogger = "LevelEdit";

        glm::vec3 ClampScale(const glm::vec3 &scale) {
            return glm::clamp(scale, glm::vec3(Shared::Entities::LevelEditEntity::kMinScale), glm::vec3(Shared::Entities::LevelEditEntity::kMaxScale));
        }

        bool SameKey(const Shared::Entities::LevelEditEntity &edit, const Shared::Game::LevelEditDefinition &definition) {
            if (edit.kind != definition.kind) {
                return false;
            }
            if (definition.kind == Shared::Game::LevelEditKind::Entity) {
                return edit.guid == definition.guid;
            }
            if (definition.kind == Shared::Game::LevelEditKind::Grass) {
                // The box is the key, to the last bit: a box a hair off is another clearing, and each client lets them overlap.
                return edit.at == definition.at && edit.halfSize == definition.halfSize && edit.yawDegrees == definition.yawDegrees;
            }
            const glm::vec3 offset = edit.at - definition.at;
            return glm::dot(offset, offset) <= Shared::Game::kLevelEditKeySlack * Shared::Game::kLevelEditKeySlack && Framework::Utils::StringUtils::EqualsIgnoreCase(edit.name, definition.name);
        }
    } // namespace

    std::vector<Shared::Entities::LevelEditEntity *> LevelEdits::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Shared::Entities::LevelEditEntity *> edits;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::LevelEditEntity>([&](Shared::Entities::LevelEditEntity *edit) {
                if (Shared::Entities::InWorld(edit, virtualWorld)) {
                    edits.push_back(edit);
                }
            });
        }
        return edits;
    }

    Shared::Entities::LevelEditEntity *LevelEdits::Find(const Shared::Game::LevelEditDefinition &definition, std::uint32_t virtualWorld) {
        // Walked in place: a map applies one entry at a time, and a list per entry would copy every edit for each.
        Shared::Entities::LevelEditEntity *found = nullptr;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::LevelEditEntity>([&](Shared::Entities::LevelEditEntity *edit) {
                if (found == nullptr && edit->GetVirtualWorld() == virtualWorld && SameKey(*edit, definition)) {
                    found = edit;
                }
            });
        }
        return found;
    }

    Shared::Entities::LevelEditEntity *LevelEdits::Apply(const Shared::Game::LevelEditDefinition &definition, std::uint32_t virtualWorld, std::string &error) {
        if (!definition.HasKey()) {
            error = definition.kind == Shared::Game::LevelEditKind::Entity ? "an entity edit needs its level guid" : definition.kind == Shared::Game::LevelEditKind::Grass ? "a grass edit needs a finite box with half sizes between 0.01 and 128 metres" : "a brush edit needs its mesh path";
            return nullptr;
        }
        if (definition.kind == Shared::Game::LevelEditKind::Grass && (definition.moved || !definition.model.empty())) {
            error = "a grass edit clears its box and nothing else: it cannot be moved or swapped for a mesh";
            return nullptr;
        }
        if (definition.name.size() > Shared::Entities::LevelEditEntity::kMaxNameLength) {
            error = "the name is longer than any path the level ships";
            return nullptr;
        }
        if (!Shared::Game::IsFinite(definition.at) || (definition.moved && (!Shared::Game::IsFinite(definition.position) || !Shared::Game::IsUsableRotation(definition.rotation) || !Shared::Game::IsFinite(definition.scale)))) {
            error = "a position, rotation or scale is not finite";
            return nullptr;
        }
        if (!definition.model.empty() && !IsSwapModel(definition.model, error)) {
            return nullptr;
        }

        Shared::Entities::LevelEditEntity *edit = Find(definition, virtualWorld);
        if (edit == nullptr) {
            auto *replication = Framework::CoreModules::GetReplication();
            edit              = replication != nullptr ? replication->CreateEntity<Shared::Entities::LevelEditEntity>() : nullptr;
            if (edit == nullptr) {
                error = "the edit could not be created";
                return nullptr;
            }

            // The key before the entity replicates, so it rides the
            // construction snapshot and never changes after it.
            edit->kind     = definition.kind;
            edit->guid     = definition.kind == Shared::Game::LevelEditKind::Entity ? definition.guid : 0;
            edit->name     = definition.name;
            edit->at       = definition.at;
            edit->halfSize   = definition.kind == Shared::Game::LevelEditKind::Grass ? definition.halfSize : glm::vec3(0.0F);
            edit->yawDegrees = definition.kind == Shared::Game::LevelEditKind::Grass ? definition.yawDegrees : 0.0F;
            edit->position   = definition.at;
            edit->SetVirtualWorld(virtualWorld);

            // The object is seen from further than anything streams, so
            // distance never takes the edit off a client; the virtual world
            // still decides who has it.
            edit->streaming.alwaysVisible = true;
        }

        edit->hidden        = definition.hidden;
        edit->model         = definition.model;
        edit->moved         = definition.moved;
        edit->movedPosition = definition.moved ? definition.position : glm::vec3(0.0F);
        edit->movedRotation = definition.moved ? glm::normalize(definition.rotation) : glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        edit->movedScale    = definition.moved ? ClampScale(definition.scale) : glm::vec3(1.0F);

        Framework::Logging::GetLogger(kLogger)->debug("Level edit {} on {} '{}' at {:.2f}, {:.2f}, {:.2f} in world {}: {}{}", edit->GetNetworkID(), Shared::Game::LevelEditKindToString(definition.kind), definition.name, definition.at.x, definition.at.y, definition.at.z, virtualWorld,
            definition.hidden ? "out" : "in", definition.moved ? ", moved" : "");
        if (!definition.model.empty()) {
            Framework::Logging::GetLogger(kLogger)->debug("Level edit {} swaps the object for '{}'", edit->GetNetworkID(), definition.model);
        }
        LevelEditModule::Get().Note(*edit);
        return edit;
    }

    bool LevelEdits::IsSwapModel(const std::string &model, std::string &error) {
        if (model.size() > Shared::Entities::LevelEditEntity::kMaxNameLength) {
            error = "the model path is longer than any path the game ships";
            return false;
        }
        // The catalog's rows and the streamed meshes are the two kinds of mesh every client can load: the same rule `Prop.spawn` keeps.
        if (Shared::Game::FindPropModel(model) == nullptr && !KCDC::Features::Prop::Props::IsStreamedModel(model)) {
            error = "model '" + model + "' is neither a prop catalog path nor a mesh this server streams";
            return false;
        }
        return true;
    }

    bool LevelEdits::SetModel(Shared::Entities::LevelEditEntity *edit, const std::string &model, std::string &error) {
        if (edit == nullptr) {
            error = "the edit is gone";
            return false;
        }
        if (edit->kind == Shared::Game::LevelEditKind::Grass && !model.empty()) {
            error = "a grass edit clears its box and cannot be swapped for a mesh";
            return false;
        }
        if (!model.empty() && !IsSwapModel(model, error)) {
            return false;
        }
        edit->model = model;
        LevelEditModule::Get().Touch();
        return true;
    }

    void LevelEdits::SetHidden(Shared::Entities::LevelEditEntity *edit, bool hidden) {
        if (edit != nullptr) {
            edit->hidden = hidden;
            LevelEditModule::Get().Touch();
        }
    }

    bool LevelEdits::Move(Shared::Entities::LevelEditEntity *edit, const glm::vec3 &position, const glm::quat &rotation, const glm::vec3 &scale) {
        // A grass edit's box is its key, fixed when it was applied: a clearing somewhere else is another edit.
        if (edit == nullptr || edit->kind == Shared::Game::LevelEditKind::Grass || !Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation) || !Shared::Game::IsFinite(scale)) {
            return false;
        }
        edit->moved         = true;
        edit->movedPosition = position;
        edit->movedRotation = glm::normalize(rotation);
        edit->movedScale    = ClampScale(scale);
        return true;
    }

    void LevelEdits::ClearMove(Shared::Entities::LevelEditEntity *edit) {
        if (edit == nullptr) {
            return;
        }
        edit->moved         = false;
        edit->movedPosition = glm::vec3(0.0F);
        edit->movedRotation = glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        edit->movedScale    = glm::vec3(1.0F);
    }

    void LevelEdits::Restore(Shared::Entities::LevelEditEntity *edit) {
        if (edit == nullptr) {
            return;
        }
        LevelEditModule::Get().Forget(*edit);
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(edit);
        }
    }

    std::size_t LevelEdits::RestoreAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        const std::vector<Shared::Entities::LevelEditEntity *> doomed = Collect(virtualWorld);
        for (Shared::Entities::LevelEditEntity *edit : doomed) {
            Restore(edit);
        }
        return doomed.size();
    }
} // namespace KCDC::Features::LevelEdit
