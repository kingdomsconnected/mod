#include "area_collision_service.h"
#include "area_collision.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/physics/pe_geomparams.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Area {
    void AreaCollisionService::Queue(std::uint64_t networkId) {
        std::scoped_lock lock(_mutex);
        _pending.insert(networkId);
    }

    void AreaCollisionService::NativeContextChanged() {
        ++_context;
    }

    void AreaCollisionService::Forget() {
        _boundaries.clear();
        std::scoped_lock lock(_mutex);
        _pending.clear();
        _observedContext = _context.load();
    }

    void AreaCollisionService::ObserveContext() {
        if (_observedContext == _context.load()) {
            return;
        }
        Forget();
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<AreaCollision>([this](AreaCollision *area) {
                Queue(area->GetNetworkID());
            });
        }
    }

    void AreaCollisionService::Remove(const std::string &key) {
        if (const auto found = _boundaries.find(key); found != _boundaries.end()) {
            if (auto *world = SDK::CPhysicalWorld::GetInstance()) {
                world->DestroyPhysicalEntity(found->second.body);
            }
            _boundaries.erase(found);
        }
    }

    void AreaCollisionService::Reset() {
        if (_observedContext != _context.load()) {
            Forget();
            return;
        }
        while (!_boundaries.empty()) {
            Remove(_boundaries.begin()->first);
        }
        std::scoped_lock lock(_mutex);
        _pending.clear();
    }

    bool AreaCollisionService::Set(const std::string &key, const Shared::Game::AreaDefinition &definition, std::string &error) {
        Shared::Game::AreaDefinition boundary;
        boundary.id               = "collision";
        boundary.shape            = definition.shape;
        boundary.collisionOptions = definition.collisionOptions;
        const auto json           = Shared::Game::AreaDefinitionToJson(boundary).dump();
        if (const auto found = _boundaries.find(key); found != _boundaries.end() && found->second.shapeJson == json) {
            return true;
        }
        Shared::Game::AreaCollisionMesh mesh;
        if (!Shared::Game::BuildAreaCollisionMesh(definition.shape, definition.collisionOptions, mesh, error)) {
            return false;
        }
        auto *world = SDK::CPhysicalWorld::GetInstance();
        if (!world) {
            error = "Physics is not ready for area collision.";
            return false;
        }
        std::vector<SDK::Vec3> vertices;
        vertices.reserve(mesh.vertices.size());
        for (const auto &point : mesh.vertices) {
            vertices.push_back({point.x, point.y, point.z});
        }
        std::vector<unsigned short> indices(mesh.indices.begin(), mesh.indices.end());
        auto *manager = world->GetGeomManager();
        // Copy buffers; preserve the separate closed wall pieces. No primitive
        // approximation: converting an enclosure to a solid box loses its inside.
        constexpr int flags = SDK::mesh_AABB | SDK::mesh_multicontact2 | SDK::mesh_no_vtx_merge | SDK::mesh_always_static;
        auto *geometry      = manager->CreateMesh({vertices.data(), sizeof(SDK::Vec3)}, {indices.data(), sizeof(unsigned short)}, nullptr, nullptr, static_cast<int>(indices.size() / 3), flags, 0.0F, 2, 4, 1.0F);
        if (!geometry) {
            error = "Could not build area collision mesh.";
            return false;
        }
        auto *registered = manager->RegisterGeometry(geometry, 0, nullptr, 0);
        manager->DestroyGeometry(geometry);
        if (!registered) {
            error = "Could not register area collision geometry.";
            return false;
        }
        auto *body = world->CreatePhysicalEntity(SDK::PE_STATIC, nullptr, nullptr, -1, -1);
        SDK::pe_geomparams params;
        params.pos     = {definition.shape.position.x, definition.shape.position.y, definition.shape.position.z};
        params.flags   = 0x8FFF;
        const int part = body ? body->AddGeometry(registered, &params, 0, 0) : -1;
        manager->UnregisterGeometry(registered);
        if (!body || part < 0) {
            if (body) {
                world->DestroyPhysicalEntity(body);
            }
            error = "Could not physicalize area boundary.";
            return false;
        }
        // A failed replacement keeps the old wall; release it only after its
        // replacement was created successfully.
        Remove(key);
        _boundaries.emplace(key, Boundary {body, json});
        return true;
    }

    bool AreaCollisionService::ReplacePreview(const std::vector<Shared::Game::AreaDefinition> &areas, std::string &error) {
        std::unordered_set<std::string> keep;
        bool success = true;
        for (const auto &area : areas) {
            if (area.collision && area.enabled) {
                keep.insert(area.id);
                if (!Set(area.id, area, error)) {
                    success = false;
                }
            }
        }
        std::vector<std::string> removed;
        for (const auto &[key, boundary] : _boundaries) {
            if (!keep.contains(key)) {
                removed.push_back(key);
            }
        }
        for (const auto &key : removed) {
            Remove(key);
        }
        return success;
    }

    void AreaCollisionService::AppendBodies(const std::vector<std::string> &ids, std::vector<SDK::IPhysicalEntity *> &bodies) const {
        for (const auto &id : ids) {
            if (const auto found = _boundaries.find(id); found != _boundaries.end()) {
                bodies.push_back(found->second.body);
            }
        }
    }

    void AreaCollisionService::Update() {
        ObserveContext();
        if (!SDK::CPhysicalWorld::GetInstance()) {
            return;
        }
        std::unordered_set<std::uint64_t> pending;
        {
            std::scoped_lock lock(_mutex);
            pending.swap(_pending);
        }
        auto *replication = Framework::CoreModules::GetReplication();
        for (const auto id : pending) {
            const auto key     = std::to_string(id);
            const auto *entity = replication ? replication->GetEntity<AreaCollision>(id) : nullptr;
            if (!entity) {
                Remove(key);
                continue;
            }
            Shared::Game::AreaDefinition definition;
            std::string error;
            const auto json = entity->definitionJson.size() <= 65536 ? nlohmann::json::parse(entity->definitionJson, nullptr, false) : nlohmann::json();
            if (!Shared::Game::AreaDefinitionFromJson(json, definition, error) || !Set(key, definition, error)) {
                Framework::Logging::GetLogger("AreaCollision")->error("Area {}: {}", id, error);
            }
        }
    }
} // namespace KCDC::Features::Area
