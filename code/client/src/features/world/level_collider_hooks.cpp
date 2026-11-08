#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "features/world/level_collider_module.h"

#include "shared/game/gate_catalog.h"

#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/patterns.h"
#include "sdk/wh/engine3d/c_physicalization_manager.h"
#include "sdk/wh/shared/c_recursive_srw_lock.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace KCDC::Features::World {
    namespace {
        using BuildLayerColliders_t = void(__fastcall *)(SDK::wh::engine3d::C_PhysicalizationManager *, const SDK::wh::engine3d::S_LayerPhysicsRecordSpan *, int);
        using DeactivateLayer_t     = void(__fastcall *)(SDK::wh::engine3d::C_PhysicalizationManager *, int);

        BuildLayerColliders_t g_buildLayerCollidersOriginal = nullptr;
        DeactivateLayer_t g_deactivateLayerOriginal         = nullptr;

        /**
         * Every build of one layer's precomputed colliders, whether its file
         * was read at once or streamed in. Both callers run on the thread that
         * loads the level or completes stream reads, and the builder
         * dereferences the span unconditionally, so it is never null.
         *
         * The colliders of the brushes gates displace are never built: a gate
         * stands in their place whenever anyone is near enough to touch them.
         *
         * The rest are built in one native call and handed over with the
         * records they came from. The lock is the one the builder takes around
         * each push, held across the build so the game thread never sees a
         * collider it cannot yet name.
         */
        void __fastcall BuildLayerColliders_Hook(SDK::wh::engine3d::C_PhysicalizationManager *manager, const SDK::wh::engine3d::S_LayerPhysicsRecordSpan *records, int layerId) {
            auto *entitySystem              = SDK::IEntitySystem::GetInstance();
            const char *layerName           = entitySystem != nullptr ? entitySystem->GetLayerName(layerId) : nullptr;
            const std::uint32_t recordCount = static_cast<std::uint32_t>(records->count);

            std::vector<SDK::wh::engine3d::S_LayerPhysicsRecord> kept;
            const SDK::wh::engine3d::S_LayerPhysicsRecordSpan *built = records;
            SDK::wh::engine3d::S_LayerPhysicsRecordSpan filtered {};
            if (layerName != nullptr && Shared::Game::IsGateLayerWithDisplacedColliders(layerName)) {
                kept.reserve(recordCount);
                for (std::uint32_t index = 0; index < recordCount; ++index) {
                    if (!Shared::Game::IsGateDisplacedCollider(layerName, index, recordCount)) {
                        kept.push_back(records->records[index]);
                    }
                }
                filtered = SDK::wh::engine3d::S_LayerPhysicsRecordSpan {kept.data(), kept.size()};
                built    = &filtered;
            }

            SDK::wh::shared::C_ScopedRecursiveSRWLock lock(manager->GetLock());
            const std::size_t before = manager->GetLayerColliders(layerId).size();
            g_buildLayerCollidersOriginal(manager, built, layerId);
            const std::span<SDK::IPhysicalEntity *const> colliders = manager->GetLayerColliders(layerId);
            LevelColliderModule::Get().NoteLayerBuilt(layerId, std::span<const SDK::wh::engine3d::S_LayerPhysicsRecord> {built->records, built->count}, colliders.subspan(std::min(before, colliders.size())));
        }

        /**
         * The one place the manager destroys colliders: a layer streaming out,
         * or every layer through `DeactivateAllLayers`. They are forgotten
         * under the same lock the destruction runs under, before any of them
         * is freed.
         */
        void __fastcall DeactivateLayer_Hook(SDK::wh::engine3d::C_PhysicalizationManager *manager, int layerId) {
            SDK::wh::shared::C_ScopedRecursiveSRWLock lock(manager->GetLock());
            LevelColliderModule::Get().ForgetLayer(layerId);
            g_deactivateLayerOriginal(manager, layerId);
        }
    } // namespace

    void LevelColliderModule::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PhysicalizationManager__BuildLayerColliders), reinterpret_cast<LPVOID>(&BuildLayerColliders_Hook), reinterpret_cast<LPVOID *>(&g_buildLayerCollidersOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_PhysicalizationManager__DeactivateLayer), reinterpret_cast<LPVOID>(&DeactivateLayer_Hook), reinterpret_cast<LPVOID *>(&g_deactivateLayerOriginal));
    }
} // namespace KCDC::Features::World

static InitFunction init(
    []() {
        KCDC::Features::World::LevelColliderModule::Install();
    },
    "LevelColliders");
