// Winsock2 must precede the Windows headers included by MinHook.
#include <utils/safe_win32.h>

#include <MinHook.h>

#include "prop_editor_layer_render.h"

#include "sdk/cryengine/3dengine/c_brush.h"
#include "sdk/cryengine/3dengine/c_vegetation.h"
#include "sdk/cryengine/system/i_console.h"
#include "sdk/patterns.h"
#include "shared/features/level_edit/level_edit_definition.h"

#include <utils/hooking/hook_function.h>

#include <cmath>
#include <cstring>
#include <utility>

namespace KCDC::Features::Prop {
    std::size_t EditorLayerRenderPolicy::CellHash::operator()(const Cell &cell) const {
        std::size_t value = std::hash<std::int64_t> {}(cell.x);
        for (const auto coordinate : {cell.y, cell.z}) {
            value ^= std::hash<std::int64_t> {}(coordinate) + 0x9e3779b9U + (value << 6) + (value >> 2);
        }
        return value;
    }

    bool EditorLayerRenderPolicy::ToCell(const SDK::Vec3 &position, Cell &cell) {
        // One-metre buckets. Guard native nonfinite input before integer conversion.
        constexpr float limit = 1.0e12F;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || std::fabs(position.x) > limit || std::fabs(position.y) > limit || std::fabs(position.z) > limit) {
            return false;
        }
        cell = {static_cast<std::int64_t>(std::floor(position.x)), static_cast<std::int64_t>(std::floor(position.y)), static_cast<std::int64_t>(std::floor(position.z))};
        return true;
    }

    void EditorLayerRenderPolicy::AddWorldGeometry(const std::string &path, const SDK::Vec3 &position, bool visible) {
        // Same five-centimetre authored key tolerance as WorldObjects::FindLevelBrush.
        constexpr float slack = Shared::Game::kLevelEditKeySlack;
        Cell first {}, last {};
        if (path.empty() || !ToCell({position.x - slack, position.y - slack, position.z - slack}, first) || !ToCell({position.x + slack, position.y + slack, position.z + slack}, last)) {
            return;
        }
        const auto index = _worldGeometry.size();
        _worldGeometry.push_back({path, position, visible});
        // Insert into adjacent buckets at boundaries: render workers perform one lookup.
        for (auto x = first.x; x <= last.x; ++x) {
            for (auto y = first.y; y <= last.y; ++y) {
                for (auto z = first.z; z <= last.z; ++z) {
                    _worldCells.emplace(Cell {x, y, z}, index);
                }
            }
        }
    }

    std::optional<bool> EditorLayerRenderPolicy::WorldGeometryVisibility(SDK::IRenderNode *liveNode) const {
        if (_worldCells.empty() || liveNode == nullptr) {
            return std::nullopt;
        }
        SDK::Vec3 position {};
        switch (liveNode->GetRenderNodeType()) {
        case SDK::eERType_Brush:
        case SDK::eERType_MovableBrush: position = static_cast<const SDK::CBrush *>(liveNode)->GetPosition(); break;
        case SDK::eERType_Vegetation: position = static_cast<const SDK::CVegetation *>(liveNode)->GetSpawnPosition(); break;
        default: return std::nullopt;
        }
        Cell cell {};
        if (!ToCell(position, cell)) {
            return std::nullopt;
        }
        const auto candidates = _worldCells.equal_range(cell);
        if (candidates.first == candidates.second || liveNode->GetOwnerEntity() != nullptr) {
            return std::nullopt;
        }
        const char *path = liveNode->GetName();
        if (path == nullptr) {
            return std::nullopt;
        }
        std::optional<bool> result;
        float nearest = Shared::Game::kLevelEditKeySlack * Shared::Game::kLevelEditKeySlack;
        for (auto candidate = candidates.first; candidate != candidates.second; ++candidate) {
            const auto &entry = _worldGeometry[candidate->second];
            const float x = position.x - entry.position.x, y = position.y - entry.position.y, z = position.z - entry.position.z;
            const float squared = x * x + y * y + z * z;
            if (squared <= nearest && _stricmp(path, entry.path.c_str()) == 0) {
                nearest = squared;
                result  = entry.visible;
            }
        }
        return result;
    }

    EditorLayerRender &EditorLayerRender::Get() {
        static EditorLayerRender instance;
        return instance;
    }

    void EditorLayerRender::Publish(std::shared_ptr<const EditorLayerRenderPolicy> policy) {
        _policy.store(std::move(policy), std::memory_order_release);
        _active.store(true, std::memory_order_release);
    }

    void EditorLayerRender::Clear() {
        _active.store(false, std::memory_order_release);
        _policy.store(nullptr, std::memory_order_release);
    }

    void EditorLayerRender::Update() {
        const auto policy  = _policy.load(std::memory_order_acquire);
        const bool hideSky = policy && !policy->defaultVisible;
        // e_SkyBox registers at CVars+0x220 (1815C0D24). The main world render
        // tests that field before the sky path 18080BEC4. This also controls
        // the native moving-cloud layers. Do not change CVars from render workers.
        UpdateOverride("e_SkyBox", hideSky, _skyBox);
        // Global fog enable is submitted each pass at 1804E6372. The native
        // temporary override owner also disables volumetric fog at 182529D52.
        UpdateOverride("e_Fog", hideSky, _fog);
        UpdateOverride("e_VolumetricFog", hideSky, _volumetricFog);
        // Hidden world geometry must not occlude visible custom-layer objects.
        // CVars+0x49C/+0x160/+0x294, registered at 1815C2D0C/1815C2894/1815C3D53.
        const bool hasHiddenContent = policy && (policy->hasHiddenLayers || !policy->defaultVisible);
        UpdateOverride("e_CoverageBuffer", hasHiddenContent, _coverageBuffer);
        UpdateOverride("e_TerrainOcclusionCulling", hasHiddenContent, _terrainOcclusion);
        UpdateOverride("e_CheckOcclusion", hasHiddenContent, _checkOcclusion);
    }

    void EditorLayerRender::UpdateOverride(const char *name, bool enabled, CVarOverride &state) {
        if (enabled && !state.applied) {
            state.previous = SDK::IConsole::GetIntCVar(name, 0);
            state.applied  = SDK::IConsole::TrySetIntCVar(name, 0);
        }
        else if (!enabled && state.applied) {
            // Preserve a console change made outside this owner while the override was active.
            if (SDK::IConsole::GetIntCVar(name, -1) == 0) {
                SDK::IConsole::TrySetIntCVar(name, state.previous);
            }
            state.applied = false;
        }
    }

    bool EditorLayerRender::ShouldRender(SDK::IRenderNode *node, const SDK::SRenderingPassInfo *pass) const {
        // Ordinary gameplay takes a single atomic read, without shared ownership
        // bookkeeping on every native draw call.
        if (!_active.load(std::memory_order_acquire)) {
            return true;
        }
        // The catalog's offscreen thumbnail pass must remain independent of scene layers.
        if (pass->auxWindow) {
            return true;
        }
        const auto policy = _policy.load(std::memory_order_acquire);
        if (!policy) {
            return true;
        }
        const auto found = policy->visibility.find(node);
        if (found != policy->visibility.end()) {
            return found->second;
        }
        // This is the live argument owned by the native render call, never a cached pointer.
        return policy->WorldGeometryVisibility(node).value_or(policy->defaultVisible);
    }

    // WHGame.dll SHA256 bdf8f9e4a11257a72b64c84700e284c29e4c4ccaf5b8d4bfa7d0b2a7294479f7.
    // Gate BEFORE native render/temp-data caches are touched. Filtering AddRenderItem
    // instead can leave an empty permanent render-object cache after a layer is restored.
    // Main-thread octree traversal and worker dispatch (18078C944) use these entries.
    void EditorLayerRender::RenderObject(void *manager, SDK::IRenderNode *node, const SDK::Vec3 *ambient, const SDK::AABB *bounds, float distance, bool occlusion, const SDK::SRenderingPassInfo *pass) {
        auto &filter = Get();
        if (filter.ShouldRender(node, pass)) {
            filter._renderObject(manager, node, ambient, bounds, distance, occlusion, pass);
        }
    }

    void EditorLayerRender::RenderObjectWithMask(void *manager, SDK::IRenderNode *node, const SDK::Vec3 *ambient, const SDK::AABB *bounds, float distance, int type, const SDK::SRenderingPassInfo *pass, std::uint64_t mask) {
        auto &filter = Get();
        if (filter.ShouldRender(node, pass)) {
            filter._renderObjectWithMask(manager, node, ambient, bounds, distance, type, pass, mask);
        }
    }

    void EditorLayerRender::RenderBrush(void *manager, SDK::IRenderNode *node, void *terrainTexture, const SDK::AABB *bounds, float distance, bool occlusion, const SDK::SRenderingPassInfo *pass, std::uint64_t mask) {
        auto &filter = Get();
        if (filter.ShouldRender(node, pass)) {
            filter._renderBrush(manager, node, terrainTexture, bounds, distance, occlusion, pass, mask);
        }
    }

    void EditorLayerRender::RenderVegetation(void *manager, SDK::IRenderNode *node, const SDK::AABB *bounds, float distance, void *terrainTexture, bool occlusion, const SDK::SRenderingPassInfo *pass, std::uint64_t mask) {
        auto &filter = Get();
        if (filter.ShouldRender(node, pass)) {
            filter._renderVegetation(manager, node, bounds, distance, terrainTexture, occlusion, pass, mask);
        }
    }

    void EditorLayerRender::RenderShadows(const SDK::SRenderingPassInfo *pass, float distance, SDK::IRenderNode *node, void *unused, std::uint64_t mask) {
        auto &filter = Get();
        if (filter.ShouldRender(node, pass)) {
            filter._renderShadows(pass, distance, node, unused, mask);
        }
    }

    void EditorLayerRender::RenderMergedMesh(SDK::IRenderNode *node, const SDK::SRenderingPassInfo *pass, std::uint64_t mask) {
        auto &filter = Get();
        if (filter.ShouldRender(node, pass)) {
            filter._renderMergedMesh(node, pass, mask);
        }
    }

    void EditorLayerRender::RenderParticles(SDK::IRenderNode *node, const SDK::SRendParams *params, const SDK::SRenderingPassInfo *pass) {
        auto &filter = Get();
        if (filter.ShouldRender(node, pass)) {
            filter._renderParticles(node, params, pass);
        }
    }

    void EditorLayerRender::RenderDeformable(void *deformable, SDK::CRenderObject *object, int lod, const SDK::AABB *bounds, const SDK::SRenderingPassInfo *pass) {
        auto &filter = Get();
        // Worker output type 3 bypasses CObjManager. Its render object is a
        // native duplicate of the brush/vegetation object, including owner identity.
        if (filter.ShouldRender(object->renderNode, pass)) {
            filter._renderDeformable(deformable, object, lod, bounds, pass);
        }
    }

    SDK::SRenderingPassInfo *EditorLayerRender::CreateGeneralPass(SDK::SRenderingPassInfo *result, std::uint32_t pipeline, const SDK::CCamera *camera, std::uint32_t flags, bool aux, const SDK::SDisplayContextKey *context) {
        auto &filter      = Get();
        const auto policy = filter._policy.load(std::memory_order_acquire);
        if (policy && !policy->defaultVisible && flags == SDK::SRenderingPassInfo::kDefaultFlags) {
            // Terrain (1804E4630), ocean/waves (180610448), clouds (1806BC490),
            // shadow setup (1804E64E0). Keep object categories for custom-layer exceptions.
            // Sky and fog use separate game-thread overrides.
            constexpr std::uint32_t worldSystems = 0x1 | 0x10 | 0x20 | 0x1000 | 0x8000;
            flags &= ~worldSystems;
        }
        return filter._createGeneralPass(result, pipeline, camera, flags, aux, context);
    }

    void EditorLayerRender::Install() {
        auto &filter = Get();
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CObjManager__RenderObject), reinterpret_cast<LPVOID>(&RenderObject), reinterpret_cast<LPVOID *>(&filter._renderObject));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CObjManager__RenderObjectWithMask), reinterpret_cast<LPVOID>(&RenderObjectWithMask), reinterpret_cast<LPVOID *>(&filter._renderObjectWithMask));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CObjManager__RenderBrush), reinterpret_cast<LPVOID>(&RenderBrush), reinterpret_cast<LPVOID *>(&filter._renderBrush));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CObjManager__RenderVegetation), reinterpret_cast<LPVOID>(&RenderVegetation), reinterpret_cast<LPVOID *>(&filter._renderVegetation));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CObjManager__RenderObjectShadows), reinterpret_cast<LPVOID>(&RenderShadows), reinterpret_cast<LPVOID *>(&filter._renderShadows));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CMergedMeshRenderNode__Render), reinterpret_cast<LPVOID>(&RenderMergedMesh), reinterpret_cast<LPVOID *>(&filter._renderMergedMesh));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CParticleEmitter__Render), reinterpret_cast<LPVOID>(&RenderParticles), reinterpret_cast<LPVOID *>(&filter._renderParticles));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CDeformableNode__Render), reinterpret_cast<LPVOID>(&RenderDeformable), reinterpret_cast<LPVOID *>(&filter._renderDeformable));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.SRenderingPassInfo__CreateGeneralPassRenderingInfo), reinterpret_cast<LPVOID>(&CreateGeneralPass), reinterpret_cast<LPVOID *>(&filter._createGeneralPass));
    }
} // namespace KCDC::Features::Prop

static InitFunction init(
    []() {
        KCDC::Features::Prop::EditorLayerRender::Install();
    },
    "EditorLayerRender");
