// Winsock2 must precede the Windows headers the SDK's hooking helpers pull in.
#include <utils/safe_win32.h>

#include "grass_module.h"

#include "common/module_support.h"
#include "sdk/cryengine/3dengine/c_obj_manager.h"
#include "sdk/cryengine/3dengine/i_3d_engine.h"

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Grass {
    namespace {
        /** Past this many nodes in one octree answer the box is split: the query takes no size and copies its whole list. */
        constexpr std::size_t kMaxNodesPerQuery = 16384;

        /** Slack over the reported count, against drift between the query's two calls. */
        constexpr std::size_t kQueryHeadroom = 64;

        /** How many times a crowded box is halved along each ground axis before what is left is given up on. */
        constexpr int kMaxSplitDepth = 4;

        bool Overlaps(const SDK::AABB &left, const SDK::AABB &right) {
            return left.min.x <= right.max.x && left.max.x >= right.min.x && left.min.y <= right.max.y && left.max.y >= right.min.y && left.min.z <= right.max.z && left.max.z >= right.min.z;
        }

        SDK::AABB BoundsOf(const KCDC::Shared::Game::GrassClearing &clearing) {
            glm::vec3 min {}, max {};
            clearing.Bounds(min, max);
            return SDK::AABB {KCDC::Common::ToNative(min), KCDC::Common::ToNative(max)};
        }

        /** The sector `node` is, when it is one and its plants are in memory. */
        SDK::CMergedMeshRenderNode *AsLoadedSector(SDK::IRenderNode *node) {
            if (node == nullptr || node->GetRenderNodeType() != SDK::eERType_MergedMesh) {
                return nullptr;
            }
            auto *sector = static_cast<SDK::CMergedMeshRenderNode *>(node);
            return sector->HasInstanceData() ? sector : nullptr;
        }

        /** Whether a clearing may touch the group: one whose plants are loaded and that the player cannot gather. */
        bool IsClearable(const SDK::SMMRMGroupHeader &group, const SDK::CObjManager &objects) {
            const SDK::IStatInstGroup *type = objects.FindStatInstGroup(group.statInstGroupId);
            return group.HasInstances() && type != nullptr && !type->pickable;
        }

        /** The octree's nodes in `box`, its merged-mesh sectors appended to `sectors`. */
        void QuerySectors(SDK::I3DEngine &engine, const SDK::AABB &box, int depth, std::vector<SDK::IRenderNode *> &nodes, std::vector<SDK::CMergedMeshRenderNode *> &sectors) {
            const std::uint32_t available = engine.GetObjectsInBox(box, nullptr);
            if (available == 0) {
                return;
            }
            if (available > kMaxNodesPerQuery) {
                if (depth >= kMaxSplitDepth) {
                    return;
                }
                const float middleX = (box.min.x + box.max.x) * 0.5F, middleY = (box.min.y + box.max.y) * 0.5F;
                QuerySectors(engine, SDK::AABB {box.min, SDK::Vec3 {middleX, middleY, box.max.z}}, depth + 1, nodes, sectors);
                QuerySectors(engine, SDK::AABB {SDK::Vec3 {middleX, box.min.y, box.min.z}, SDK::Vec3 {box.max.x, middleY, box.max.z}}, depth + 1, nodes, sectors);
                QuerySectors(engine, SDK::AABB {SDK::Vec3 {box.min.x, middleY, box.min.z}, SDK::Vec3 {middleX, box.max.y, box.max.z}}, depth + 1, nodes, sectors);
                QuerySectors(engine, SDK::AABB {SDK::Vec3 {middleX, middleY, box.min.z}, box.max}, depth + 1, nodes, sectors);
                return;
            }
            nodes.assign(static_cast<std::size_t>(available) + kQueryHeadroom, nullptr);
            const std::size_t returned = std::min<std::size_t>(engine.GetObjectsInBox(box, nodes.data()), nodes.size());
            for (std::size_t index = 0; index < returned; ++index) {
                SDK::CMergedMeshRenderNode *sector = AsLoadedSector(nodes[index]);
                if (sector != nullptr && std::find(sectors.begin(), sectors.end(), sector) == sectors.end()) {
                    sectors.push_back(sector);
                }
            }
        }
    } // namespace

    GrassModule &GrassModule::Get() {
        static GrassModule instance;
        return instance;
    }

    void GrassModule::SyncListening() {
        auto *engine                       = SDK::I3DEngine::GetInstance();
        SDK::IMergedMeshesManager *manager = engine != nullptr ? engine->GetIMergedMeshesManager() : nullptr;
        if (_clearings.empty()) {
            if (_listeningTo != nullptr && _listeningTo == manager) {
                manager->RemoveStreamListener(this);
            }
            _listeningTo = nullptr;
            return;
        }
        if (manager != nullptr) {
            // Off first: the list takes the same pointer twice, and a level
            // change may have emptied it behind a manager that stayed.
            manager->RemoveStreamListener(this);
            manager->AddStreamListener(this);
        }
        _listeningTo = manager;
    }

    void GrassModule::CollectSectors(const KCDC::Shared::Game::GrassClearing &clearing) {
        _sectors.clear();
        if (auto *engine = SDK::I3DEngine::GetInstance()) {
            QuerySectors(*engine, BoundsOf(clearing), 0, _nodes, _sectors);
        }
    }

    std::size_t GrassModule::Hide(SDK::CMergedMeshRenderNode &sector, const KCDC::Shared::Game::GrassClearing &clearing) {
        const SDK::CObjManager *objects = SDK::CObjManager::GetInstance();
        if (objects == nullptr || !sector.HasInstanceData()) {
            return 0;
        }
        const float sine = std::sin(glm::radians(clearing.yawDegrees)), cosine = std::cos(glm::radians(clearing.yawDegrees));
        std::size_t hidden = 0;
        for (std::uint32_t groupIndex = 0; groupIndex < sector.GetGroupCount(); ++groupIndex) {
            SDK::SMMRMGroupHeader &group = sector.GetGroup(groupIndex);
            if (!IsClearable(group, *objects)) {
                continue;
            }
            for (std::uint32_t index = 0; index < group.instanceCount; ++index) {
                SDK::SMMRMInstance &plant = group.instances[index];
                if (!plant.IsHidden() && clearing.Contains(KCDC::Common::ToGlm(sector.DecodePosition(plant)), sine, cosine)) {
                    plant.Hide();
                    ++hidden;
                }
            }
        }
        return hidden;
    }

    std::size_t GrassModule::Restore(SDK::CMergedMeshRenderNode &sector, const KCDC::Shared::Game::GrassClearing &removed) const {
        const SDK::CObjManager *objects = SDK::CObjManager::GetInstance();
        if (objects == nullptr || !sector.HasInstanceData()) {
            return 0;
        }
        const float sine = std::sin(glm::radians(removed.yawDegrees)), cosine = std::cos(glm::radians(removed.yawDegrees));
        std::size_t restored = 0;
        for (std::uint32_t groupIndex = 0; groupIndex < sector.GetGroupCount(); ++groupIndex) {
            SDK::SMMRMGroupHeader &group = sector.GetGroup(groupIndex);
            if (!IsClearable(group, *objects)) {
                continue;
            }
            for (std::uint32_t index = 0; index < group.instanceCount; ++index) {
                SDK::SMMRMInstance &plant = group.instances[index];
                if (!plant.IsHidden() || plant.loadedScale == 0) {
                    continue;
                }
                const glm::vec3 position = KCDC::Common::ToGlm(sector.DecodePosition(plant));
                if (!removed.Contains(position, sine, cosine)) {
                    continue;
                }
                const bool held = std::any_of(_clearings.begin(), _clearings.end(), [&position](const auto &entry) {
                    return entry.second.Contains(position);
                });
                if (!held) {
                    plant.Restore();
                    ++restored;
                }
            }
        }
        return restored;
    }

    std::uint64_t GrassModule::Add(const KCDC::Shared::Game::GrassClearing &clearing, GrassChange *change) {
        if (!clearing.IsSane()) {
            return 0;
        }
        const std::uint64_t id = _nextId++;
        _clearings.emplace(id, clearing);
        SyncListening();

        CollectSectors(clearing);
        std::size_t plants = 0;
        for (SDK::CMergedMeshRenderNode *sector : _sectors) {
            plants += Hide(*sector, clearing);
        }
        if (change != nullptr) {
            *change = GrassChange {_sectors.size(), plants};
        }
        return id;
    }

    bool GrassModule::Remove(std::uint64_t id, GrassChange *change) {
        const auto found = _clearings.find(id);
        if (found == _clearings.end()) {
            return false;
        }
        const KCDC::Shared::Game::GrassClearing removed = found->second;
        _clearings.erase(found);

        CollectSectors(removed);
        std::size_t plants = 0;
        for (SDK::CMergedMeshRenderNode *sector : _sectors) {
            plants += Restore(*sector, removed);
        }
        if (change != nullptr) {
            *change = GrassChange {_sectors.size(), plants};
        }
        SyncListening();
        return true;
    }

    void GrassModule::OnMergedMeshStreamedIn(SDK::IRenderNode *node) {
        SDK::CMergedMeshRenderNode *sector = AsLoadedSector(node);
        if (sector == nullptr) {
            return;
        }
        for (const auto &[id, clearing] : _clearings) {
            // A turned sector's plants are not where its cube is; it is rare enough to weigh plant by plant.
            if (sector->IsTurned() || Overlaps(BoundsOf(clearing), sector->GetSectorBounds())) {
                Hide(*sector, clearing);
            }
        }
    }

    void GrassModule::OnMergedMeshStreamedOut(SDK::IRenderNode *) {
        // Nothing is kept about a sector: its plants go, and come back as the file has them.
    }
} // namespace KCDC::Features::Grass
