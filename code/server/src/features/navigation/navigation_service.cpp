#include "navigation_service.h"
#include "navmesh_discovery.h"


#include <logging/logger.h>


#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

namespace KCDC::Features::Navigation {
    namespace {
        constexpr const char *kLogger = "Navigation";

    } // namespace

    bool NavigationService::ReadPak(const std::string &path, MeshFiles &base, std::map<std::string, OverlayIndex> &overlays, std::string &error) {
        return Navmesh::ReadPak(path, base, overlays, error);
    }

    void NavigationService::Install(const std::string &level, const std::string &source, fu2::function<const KCDC::Shared::Entities::DoorEntity *(std::uint64_t) const> findDoor) {
        _findDoor      = std::move(findDoor);
        _unlockedDoors = [this](const EntityLink &link) {
            return MayCross(DoorPolicy::Unlocked, link, link.kind == TileCodec::kDoorLinkKind ? _findDoor(link.entityGuid) : nullptr);
        };
        _openDoors = [this](const EntityLink &link) {
            return MayCross(DoorPolicy::Open, link, link.kind == TileCodec::kDoorLinkKind ? _findDoor(link.entityGuid) : nullptr);
        };
        _noDoors = [](const EntityLink &link) {
            return MayCross(DoorPolicy::None, link, nullptr);
        };

        std::error_code filesystemError;
        const std::filesystem::path serverDirectory = std::filesystem::current_path(filesystemError);
        for (const std::filesystem::path &pak : FindNavmeshPaks(source, level, serverDirectory)) {
            const auto started = std::chrono::steady_clock::now();
            std::string error;
            std::vector<std::string> rejected;
            if (!_mesh.LoadPak(pak.string(), level, error, &rejected)) {
                Framework::Logging::GetLogger(kLogger)->warn("Could not load {}: {}; checking the next navigation mesh candidate", pak.string(), error);
                continue;
            }

            for (const auto &name : rejected) {
                Framework::Logging::GetLogger(kLogger)->warn("Rejected navigation overlay {} in {}", name, pak.string());
            }
            _sourcePath        = pak.string();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            Framework::Logging::GetLogger(kLogger)->info("Loaded the {} navigation mesh from {}: {} tiles, {} polygons, {} overlays, in {} ms", level, _sourcePath, _mesh.TileCount(), _mesh.PolygonCount(), _mesh.OverlayNames().size(), elapsed);
            Framework::Logging::GetLogger(kLogger)->info("Gated {} catalogued doors the mesh marks no doorway for, across {} polygons under their leaves", _mesh.SyntheticDoorCount(), _mesh.SyntheticDoorwayCount());
            return;
        }
        const std::filesystem::path searched = std::filesystem::absolute(serverDirectory / "files", filesystemError);
        Framework::Logging::GetLogger(kLogger)->warn("No usable navigation mesh found for '{}' in {}; copy its recast.pak into {} or set mod.navmesh. Navigation.ready is false", level, searched.string(), (searched / level).string());
    }

    std::vector<Navmesh::DoorLeaf> NavigationService::DoorLeaves(const std::string &level) {
        return Navmesh::DoorLeaves(level);
    }

    bool NavigationService::EnableOverlay(const std::string &name) {
        std::string error;
        const bool enabled = _mesh.EnableOverlay(name, error);
        if (!error.empty()) {
            Framework::Logging::GetLogger(kLogger)->warn("Could not enable {}", error);
        }
        return enabled;
    }

    bool NavigationService::DisableOverlay(const std::string &name) {
        std::string error;
        const bool disabled = _mesh.DisableOverlay(name, error);
        if (!error.empty()) {
            Framework::Logging::GetLogger(kLogger)->warn("Disabled overlay {}, but a column could not be put back: {}", name, error);
        }
        return disabled;
    }

    bool NavigationService::MayCross(DoorPolicy policy, const EntityLink &link, const KCDC::Shared::Entities::DoorEntity *door) {
        if (link.kind != TileCodec::kDoorLinkKind) {
            return true;
        }
        switch (policy) {
        case DoorPolicy::Unlocked: return door == nullptr || !door->locked;
        case DoorPolicy::Open: return door == nullptr || door->open;
        case DoorPolicy::Any: return true;
        case DoorPolicy::None: return false;
        }
        return true;
    }

    QueryOptions NavigationService::Options(DoorPolicy policy, float searchRadius, float searchHeight, int maxSearchNodes) const {
        QueryOptions options;
        options.searchRadius   = searchRadius;
        options.searchHeight   = searchHeight;
        options.maxSearchNodes = maxSearchNodes;
        switch (policy) {
        case DoorPolicy::Unlocked: options.linkGate = &_unlockedDoors; break;
        case DoorPolicy::Open: options.linkGate = &_openDoors; break;
        case DoorPolicy::None: options.linkGate = &_noDoors; break;
        case DoorPolicy::Any: options.linkGate = nullptr; break;
        }
        return options;
    }
} // namespace KCDC::Features::Navigation
