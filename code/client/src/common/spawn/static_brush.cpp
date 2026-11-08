#include "static_brush.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/3dengine/i_brush.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/physics/pe_geomparams.h"

#include <utility>

namespace KCDC::Common::Spawn {
    StaticBrushRegistry &StaticBrushRegistry::Get() {
        static StaticBrushRegistry registry;
        return registry;
    }

    void StaticBrushRegistry::Add(const SDK::IRenderNode *node) {
        std::scoped_lock lock(_mutex);
        _nodes.insert(node);
    }

    void StaticBrushRegistry::Remove(const SDK::IRenderNode *node) {
        std::scoped_lock lock(_mutex);
        _nodes.erase(node);
    }

    bool StaticBrushRegistry::Owns(const SDK::IRenderNode *node) const {
        std::scoped_lock lock(_mutex);
        return node != nullptr && _nodes.contains(node);
    }

    StaticBrush::StaticBrush(StaticBrush &&other) noexcept: _brush(std::exchange(other._brush, nullptr)), _mesh(std::exchange(other._mesh, nullptr)), _collider(std::exchange(other._collider, nullptr)), _collide(other._collide) {}

    StaticBrush &StaticBrush::operator=(StaticBrush &&other) noexcept {
        if (this != &other) {
            _brush    = std::exchange(other._brush, nullptr);
            _mesh     = std::exchange(other._mesh, nullptr);
            _collider = std::exchange(other._collider, nullptr);
            _collide  = other._collide;
        }
        return *this;
    }

    StaticBrush::BuildResult StaticBrush::Build(const char *model, const SDK::Matrix34 &matrix, bool collide) {
        Destroy();

        SDK::I3DEngine *engine = SDK::I3DEngine::GetInstance();
        if (engine == nullptr) {
            return BuildResult::NoEngine;
        }

        // Streamed like the level's own meshes: the file and its collision
        // are read now, the render LODs as the camera nears them.
        SDK::IStatObj *mesh = engine->LoadStatObj(model, nullptr, nullptr, true, 0);
        if (mesh == nullptr) {
            return BuildResult::MeshRefused;
        }

        SDK::IRenderNode *node = engine->CreateRenderNode(SDK::eERType_Brush);
        if (node == nullptr) {
            return BuildResult::NoEngine;
        }
        auto *brush = static_cast<SDK::IBrush *>(node);

        // Before the mesh: `SetEntityStatObj` and every later `SetMatrix`
        // physicalise a brush whose bit is clear. Disable that path because
        // BuildCollider owns the collider independently of the on-demand grid.
        brush->DisablePhysicalization(true);

        // What a level loader gives a shadow-casting brush. Before the first
        // registration, which `SetEntityStatObj` performs through `SetMatrix`.
        node->AddRenderFlagsBeforeRegistration(SDK::ERF_CASTSHADOWMAPS | SDK::ERF_HAS_CASTSHADOWMAPS);

        // The mesh and its final transform in one call, so the collider is
        // built where the brush stands rather than at the origin and moved.
        node->SetEntityStatObj(mesh, &matrix);

        // `SetMatrix` returns early for a matrix the node already has, the
        // identity included; registering again is idempotent.
        engine->RegisterEntity(node);
        StaticBrushRegistry::Get().Add(node);

        _brush   = node;
        _mesh    = mesh;
        _collide = collide;
        if (collide && !BuildCollider(matrix)) {
            return BuildResult::BuiltWithoutCollider;
        }
        return BuildResult::Built;
    }

    void StaticBrush::SetMatrix(const SDK::Matrix34 &matrix) {
        if (_brush == nullptr) {
            return;
        }
        _brush->SetMatrix(matrix);
        if (_collide) {
            BuildCollider(matrix);
        }
    }

    bool StaticBrush::BuildCollider(const SDK::Matrix34 &matrix) {
        DestroyCollider();

        SDK::CPhysicalWorld *world = SDK::CPhysicalWorld::GetInstance();
        if (world == nullptr || _mesh == nullptr || _brush == nullptr) {
            return false;
        }

        // What `CBrush::PhysicalizeOnHeap` builds for itself (0x180530A3C): a
        // static body whose foreign data is the brush, so a ray, a blow or an
        // arrow meets it as the brush it is.
        SDK::IPhysicalEntity *body = world->CreatePhysicalEntity(SDK::PE_STATIC, nullptr, _brush, SDK::PHYS_FOREIGN_ID_STATIC, -1);
        if (body == nullptr) {
            return false;
        }

        // The mesh's own physics geometry, every part of a compound included,
        // placed by the full world matrix: the body itself stays at the origin.
        SDK::Matrix34 placed = matrix;
        SDK::pe_geomparams params;
        params.density = -1.0F;
        params.mass    = -1.0F;
        params.pMtx3x4 = &placed;
        _mesh->Physicalize(body, &params);

        SDK::pe_status_nparts parts;
        if (body->GetStatus(&parts) <= 0) {
            world->DestroyPhysicalEntity(body);
            return false;
        }
        _collider = body;
        return true;
    }

    void StaticBrush::DestroyCollider() {
        if (_collider == nullptr) {
            return;
        }
        if (SDK::CPhysicalWorld *world = SDK::CPhysicalWorld::GetInstance()) {
            world->DestroyPhysicalEntity(_collider);
        }
        _collider = nullptr;
    }

    void StaticBrush::Destroy() {
        // Before the brush: the body's foreign data points at it.
        DestroyCollider();
        _mesh = nullptr;

        if (_brush != nullptr) {
            StaticBrushRegistry::Get().Remove(_brush);
            if (auto *engine = SDK::I3DEngine::GetInstance()) {
                engine->DeleteRenderNode(_brush);
            }
            _brush = nullptr;
        }
    }

    void StaticBrush::DestroyReleased(SDK::IRenderNode *brush, SDK::IPhysicalEntity *collider) {
        auto *world = SDK::CPhysicalWorld::GetInstance();
        if (collider != nullptr && world != nullptr) {
            world->DestroyPhysicalEntity(collider);
        }
        auto *engine = SDK::I3DEngine::GetInstance();
        if (brush != nullptr && engine != nullptr) {
            engine->DeleteRenderNode(brush);
        }
    }

    void StaticBrush::Forget() {
        // The address may be handed to one of the next level's own brushes.
        if (_brush != nullptr) {
            StaticBrushRegistry::Get().Remove(_brush);
        }
        _brush    = nullptr;
        _mesh     = nullptr;
        _collider = nullptr;
    }

    void StaticBrush::Release(SDK::IRenderNode *&brush, SDK::IPhysicalEntity *&collider) {
        if (_brush != nullptr) {
            StaticBrushRegistry::Get().Remove(_brush);
        }
        brush    = std::exchange(_brush, nullptr);
        collider = std::exchange(_collider, nullptr);
        _mesh    = nullptr;
    }
} // namespace KCDC::Common::Spawn
