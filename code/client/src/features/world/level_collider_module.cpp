#include "level_collider_module.h"

#include "shared/features/level_edit/level_edit_definition.h"

#include "sdk/cryengine/3dengine/c_3d_engine.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/physics/i_physical_world.h"
#include "sdk/wh/shared/c_recursive_srw_lock.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

namespace KCDC::Features::World {
    namespace {
        bool SamePlacement(const SDK::Vec3 &left, const SDK::Vec3 &right) {
            const float x = left.x - right.x;
            const float y = left.y - right.y;
            const float z = left.z - right.z;
            return x * x + y * y + z * z <= Shared::Game::kLevelEditKeySlack * Shared::Game::kLevelEditKeySlack;
        }
    } // namespace

    LevelColliderModule &LevelColliderModule::Get() {
        static LevelColliderModule instance;
        return instance;
    }

    LevelColliderModule::Held *LevelColliderModule::FindHeld(const SDK::IStatObj *statObj, const SDK::Vec3 &position) {
        const auto placements = _held.find(statObj);
        if (placements == _held.end()) {
            return nullptr;
        }
        const auto found = std::find_if(placements->second.begin(), placements->second.end(), [&](const Held &held) {
            return SamePlacement(held.position, position);
        });
        return found != placements->second.end() ? &*found : nullptr;
    }

    void LevelColliderModule::SetSuspended(const SDK::IStatObj *statObj, const SDK::Vec3 &position, bool suspended) {
        const auto colliders       = _colliders.find(statObj);
        SDK::CPhysicalWorld *world = SDK::CPhysicalWorld::GetInstance();
        if (colliders == _colliders.end() || world == nullptr) {
            return;
        }
        // Both modes are idempotent, and a suspended collider is still
        // destroyed normally when the manager tears its layer down.
        for (const Collider &collider : colliders->second) {
            if (SamePlacement(collider.position, position)) {
                world->DestroyPhysicalEntity(collider.entity, suspended ? SDK::DM_SUSPEND : SDK::DM_RESTORE);
            }
        }
    }

    void LevelColliderModule::NoteLayerBuilt(int layerId, std::span<const SDK::wh::engine3d::S_LayerPhysicsRecord> records, std::span<SDK::IPhysicalEntity *const> built) {
        const SDK::C3DEngine *engine                 = SDK::C3DEngine::GetInstance();
        const SDK::LevelStatObjTableLayout *statObjs = engine != nullptr ? engine->GetLevelStatObjTable() : nullptr;
        if (statObjs == nullptr) {
            return;
        }

        // Every record the builder made a collider for, in its order. Were the
        // counts ever to disagree, the builder skipped for a reason this does
        // not know, and nothing is paired rather than pairing wrongly.
        const std::size_t buildable = static_cast<std::size_t>(std::count_if(records.begin(), records.end(), [statObjs](const SDK::wh::engine3d::S_LayerPhysicsRecord &record) {
            return statObjs->At(static_cast<std::int32_t>(record.staticObjectIndex)) != nullptr;
        }));
        if (buildable != built.size()) {
            return;
        }

        SDK::CPhysicalWorld *world = SDK::CPhysicalWorld::GetInstance();
        std::size_t next           = 0;
        for (const SDK::wh::engine3d::S_LayerPhysicsRecord &record : records) {
            const SDK::IStatObj *statObj = statObjs->At(static_cast<std::int32_t>(record.staticObjectIndex));
            if (statObj == nullptr) {
                continue;
            }
            const Collider collider {layerId, SDK::Vec3 {record.worldTransform.m[0][3], record.worldTransform.m[1][3], record.worldTransform.m[2][3]}, built[next++]};
            _colliders[statObj].push_back(collider);
            _placements[collider.entity] = Placement {statObj, collider.position};

            if (world != nullptr && FindHeld(statObj, collider.position) != nullptr) {
                world->DestroyPhysicalEntity(collider.entity, SDK::DM_SUSPEND);
            }
        }
    }

    void LevelColliderModule::ForgetLayer(int layerId) {
        for (auto it = _colliders.begin(); it != _colliders.end();) {
            std::erase_if(it->second, [this, layerId](const Collider &collider) {
                if (collider.layerId != layerId) {
                    return false;
                }
                _placements.erase(collider.entity);
                return true;
            });
            it = it->second.empty() ? _colliders.erase(it) : std::next(it);
        }
    }

    bool LevelColliderModule::Hold(SDK::IStatObj *statObj, const SDK::Vec3 &position) {
        auto *manager = SDK::wh::engine3d::C_PhysicalizationManager::GetInstance();
        if (manager == nullptr || statObj == nullptr) {
            return false;
        }
        SDK::wh::shared::C_ScopedRecursiveSRWLock lock(manager->GetLock());

        if (Held *held = FindHeld(statObj, position)) {
            ++held->count;
            return true;
        }
        _held[statObj].push_back(Held {position, 1});
        SetSuspended(statObj, position, true);
        return true;
    }

    bool LevelColliderModule::Release(SDK::IStatObj *statObj, const SDK::Vec3 &position) {
        auto *manager = SDK::wh::engine3d::C_PhysicalizationManager::GetInstance();
        if (manager == nullptr || statObj == nullptr) {
            return false;
        }
        SDK::wh::shared::C_ScopedRecursiveSRWLock lock(manager->GetLock());

        Held *held = FindHeld(statObj, position);
        if (held == nullptr || --held->count != 0) {
            return false;
        }
        std::vector<Held> &placements = _held[statObj];
        placements.erase(placements.begin() + (held - placements.data()));
        if (placements.empty()) {
            _held.erase(statObj);
        }
        SetSuspended(statObj, position, false);
        return true;
    }

    std::optional<LevelColliderModule::Placement> LevelColliderModule::FindPlacement(const SDK::IPhysicalEntity *collider) {
        auto *manager = SDK::wh::engine3d::C_PhysicalizationManager::GetInstance();
        if (manager == nullptr) {
            return std::nullopt;
        }
        SDK::wh::shared::C_ScopedRecursiveSRWLock lock(manager->GetLock());

        const auto found = _placements.find(collider);
        return found != _placements.end() ? std::optional<Placement>(found->second) : std::nullopt;
    }
} // namespace KCDC::Features::World
