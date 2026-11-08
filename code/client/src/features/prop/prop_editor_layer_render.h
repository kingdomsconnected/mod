#pragma once

#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/cryengine/3dengine/s_rendering_pass_info.h"
#include "sdk/cryengine/renderer/c_render_object.h"
#include "sdk/cryengine/renderer/s_rend_params.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Prop {
    /** Game-thread snapshot. Keys are identities only; render hooks never dereference them. */
    struct EditorLayerRenderPolicy {
        bool defaultVisible  = true;
        bool hasHiddenLayers = false;
        std::unordered_map<const SDK::IRenderNode *, bool> visibility;

        /** Stable authored keys for streamed world geometry; no retained native pointers. */
        void AddWorldGeometry(const std::string &path, const SDK::Vec3 &position, bool visible);
        std::optional<bool> WorldGeometryVisibility(SDK::IRenderNode *liveNode) const;

      private:
        struct Cell {
            std::int64_t x, y, z;
            bool operator==(const Cell &) const = default;
        };
        struct CellHash {
            std::size_t operator()(const Cell &cell) const;
        };
        struct WorldGeometry {
            std::string path;
            SDK::Vec3 position;
            bool visible;
        };
        static bool ToCell(const SDK::Vec3 &position, Cell &cell);
        std::vector<WorldGeometry> _worldGeometry;
        std::unordered_multimap<Cell, std::size_t, CellHash> _worldCells;
    };

    /** Temporary editor draw filtering, without changing native hidden flags or authoring edits. */
    class EditorLayerRender final {
      public:
        static EditorLayerRender &Get();

        /** Publish after resolving live nodes. Never mutate a published snapshot. */
        void Publish(std::shared_ptr<const EditorLayerRenderPolicy> policy);
        /** Call before releasing/rebuilding nodes, switching projects, or leaving the editor. */
        void Clear();
        /** Game thread, every tick including editor-inactive ticks: applies/restores native settings. */
        void Update();
        static void Install();

      private:
        struct CVarOverride {
            int previous = 0;
            bool applied = false;
        };
        static void UpdateOverride(const char *name, bool enabled, CVarOverride &state);
        bool ShouldRender(SDK::IRenderNode *node, const SDK::SRenderingPassInfo *pass) const;

        using RenderObjectFn         = void (*)(void *, SDK::IRenderNode *, const SDK::Vec3 *, const SDK::AABB *, float, bool, const SDK::SRenderingPassInfo *);
        using RenderObjectWithMaskFn = void (*)(void *, SDK::IRenderNode *, const SDK::Vec3 *, const SDK::AABB *, float, int, const SDK::SRenderingPassInfo *, std::uint64_t);
        using RenderBrushFn          = void (*)(void *, SDK::IRenderNode *, void *, const SDK::AABB *, float, bool, const SDK::SRenderingPassInfo *, std::uint64_t);
        using RenderVegetationFn     = void (*)(void *, SDK::IRenderNode *, const SDK::AABB *, float, void *, bool, const SDK::SRenderingPassInfo *, std::uint64_t);
        using RenderShadowsFn        = void (*)(const SDK::SRenderingPassInfo *, float, SDK::IRenderNode *, void *, std::uint64_t);
        using RenderMergedMeshFn     = void (*)(SDK::IRenderNode *, const SDK::SRenderingPassInfo *, std::uint64_t);
        using RenderParticlesFn      = void (*)(SDK::IRenderNode *, const SDK::SRendParams *, const SDK::SRenderingPassInfo *);
        using RenderDeformableFn     = void (*)(void *, SDK::CRenderObject *, int, const SDK::AABB *, const SDK::SRenderingPassInfo *);
        using CreateGeneralPassFn    = SDK::SRenderingPassInfo *(*)(SDK::SRenderingPassInfo *, std::uint32_t, const SDK::CCamera *, std::uint32_t, bool, const SDK::SDisplayContextKey *);

        static void RenderObject(void *, SDK::IRenderNode *, const SDK::Vec3 *, const SDK::AABB *, float, bool, const SDK::SRenderingPassInfo *);
        static void RenderObjectWithMask(void *, SDK::IRenderNode *, const SDK::Vec3 *, const SDK::AABB *, float, int, const SDK::SRenderingPassInfo *, std::uint64_t);
        static void RenderBrush(void *, SDK::IRenderNode *, void *, const SDK::AABB *, float, bool, const SDK::SRenderingPassInfo *, std::uint64_t);
        static void RenderVegetation(void *, SDK::IRenderNode *, const SDK::AABB *, float, void *, bool, const SDK::SRenderingPassInfo *, std::uint64_t);
        static void RenderShadows(const SDK::SRenderingPassInfo *, float, SDK::IRenderNode *, void *, std::uint64_t);
        static void RenderMergedMesh(SDK::IRenderNode *, const SDK::SRenderingPassInfo *, std::uint64_t);
        static void RenderParticles(SDK::IRenderNode *, const SDK::SRendParams *, const SDK::SRenderingPassInfo *);
        static void RenderDeformable(void *, SDK::CRenderObject *, int, const SDK::AABB *, const SDK::SRenderingPassInfo *);
        static SDK::SRenderingPassInfo *CreateGeneralPass(SDK::SRenderingPassInfo *, std::uint32_t, const SDK::CCamera *, std::uint32_t, bool, const SDK::SDisplayContextKey *);

        std::atomic<std::shared_ptr<const EditorLayerRenderPolicy>> _policy;
        std::atomic<bool> _active {false};
        RenderObjectFn _renderObject                 = nullptr;
        RenderObjectWithMaskFn _renderObjectWithMask = nullptr;
        RenderBrushFn _renderBrush                   = nullptr;
        RenderVegetationFn _renderVegetation         = nullptr;
        RenderShadowsFn _renderShadows               = nullptr;
        RenderMergedMeshFn _renderMergedMesh         = nullptr;
        RenderParticlesFn _renderParticles           = nullptr;
        RenderDeformableFn _renderDeformable         = nullptr;
        CreateGeneralPassFn _createGeneralPass       = nullptr;
        CVarOverride _skyBox;
        CVarOverride _fog;
        CVarOverride _volumetricFog;
        CVarOverride _coverageBuffer;
        CVarOverride _terrainOcclusion;
        CVarOverride _checkOcclusion;
    };
} // namespace KCDC::Features::Prop
