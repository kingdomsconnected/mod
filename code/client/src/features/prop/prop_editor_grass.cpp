#include "prop_editor_grass.h"

#include <algorithm>

namespace KCDC::Features::Prop {
    std::uint64_t EditorGrass::Add(const Shared::Game::GrassClearing &box, KCDC::Features::Grass::GrassChange *change) {
        const std::uint64_t applied = KCDC::Features::Grass::GrassModule::Get().Add(box, change);
        if (applied == 0) {
            return 0;
        }
        _clearings.push_back(PlacedClearing {_nextId++, box, applied});
        return _clearings.back().id;
    }

    bool EditorGrass::Restore(const PlacedClearing &clearing) {
        if (clearing.id == 0 || Find(clearing.id) != nullptr) {
            return false;
        }
        const std::uint64_t applied = KCDC::Features::Grass::GrassModule::Get().Add(clearing.box);
        if (applied == 0) {
            return false;
        }
        _clearings.push_back(PlacedClearing {clearing.id, clearing.box, applied});
        _nextId = std::max(_nextId, clearing.id + 1);
        return true;
    }

    bool EditorGrass::Remove(std::uint64_t id) {
        const auto found = std::find_if(_clearings.begin(), _clearings.end(), [id](const PlacedClearing &clearing) {
            return clearing.id == id;
        });
        if (found == _clearings.end()) {
            return false;
        }
        KCDC::Features::Grass::GrassModule::Get().Remove(found->applied);
        _clearings.erase(found);
        return true;
    }

    void EditorGrass::Clear() {
        for (const PlacedClearing &clearing : _clearings) {
            KCDC::Features::Grass::GrassModule::Get().Remove(clearing.applied);
        }
        _clearings.clear();
    }

    void EditorGrass::Forget() {
        // The module is still told: it stops listening once its last clearing goes.
        Clear();
    }

    const PlacedClearing *EditorGrass::Find(std::uint64_t id) const {
        const auto found = std::find_if(_clearings.begin(), _clearings.end(), [id](const PlacedClearing &clearing) {
            return clearing.id == id;
        });
        return found != _clearings.end() ? &*found : nullptr;
    }

    const PlacedClearing *EditorGrass::FindBox(const Shared::Game::GrassClearing &box) const {
        const auto found = std::find_if(_clearings.begin(), _clearings.end(), [&box](const PlacedClearing &clearing) {
            return clearing.box == box;
        });
        return found != _clearings.end() ? &*found : nullptr;
    }

    void EditorGrass::ToDocument(std::vector<Shared::Game::LevelEditDefinition> &edits) const {
        for (const PlacedClearing &clearing : _clearings) {
            Shared::Game::LevelEditDefinition entry;
            entry.kind       = Shared::Game::LevelEditKind::Grass;
            entry.at         = clearing.box.center;
            entry.halfSize   = clearing.box.halfSize;
            entry.yawDegrees = clearing.box.yawDegrees;
            entry.hidden     = true;
            edits.push_back(std::move(entry));
        }
    }
} // namespace KCDC::Features::Prop
