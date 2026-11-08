#include "map_service.h"

#include "features/map/map_scripting.h"

#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/guimodule/c_ui_map.h"
#include "sdk/wh/guimodule/c_ui_state_blockers.h"
#include "sdk/wh/guimodule/e_compass_mark_type.h"
#include "sdk/wh/playermodule/c_fast_travel.h"
#include "sdk/wh/playermodule/c_player_module.h"
#include "sdk/wh/rpgmodule/c_rpg_module.h"

#include <logging/logger.h>

#include <cstring>

namespace KCDC::Features::Map {
    namespace {
        constexpr const char *kLogger = "Map";
    } // namespace

    void MapService::NotifyMarkerPlaced(const SDK::Vec3 &position, std::int32_t mapId, bool moved) {
        // Nothing goes out on the wire: a marker is one player's own note to
        // himself until a resource decides to share it.
        Framework::Logging::GetLogger(kLogger)->debug("Map marker {} on map {} at ({:.2f}, {:.2f}, {:.2f})", moved ? "moved" : "placed", mapId, position.x, position.y, position.z);
        Scripting::Events::MapWaypointSet(glm::vec3 {position.x, position.y, position.z}, mapId, moved);
    }

    void MapService::NotifyMarkerCleared(const SDK::Vec3 &position, std::int32_t mapId) {
        Framework::Logging::GetLogger(kLogger)->debug("Map marker cleared on map {} at ({:.2f}, {:.2f}, {:.2f})", mapId, position.x, position.y, position.z);
        Scripting::Events::MapWaypointCleared(glm::vec3 {position.x, position.y, position.z}, mapId);
    }

    void MapService::NotifyScreenChanged(bool open) {
        if (open) {
            Scripting::Events::MapOpened();
        }
        else {
            Scripting::Events::MapClosed();
        }
    }

    bool MapService::IsMapOpen() const {
        SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        return map != nullptr && map->IsOpen();
    }

    bool MapService::SetCompassVisible(bool visible) {
        if (visible == IsCompassVisible()) {
            return true;
        }

        SDK::wh::guimodule::C_UIStateBlockers *blockers = SDK::wh::guimodule::C_UIStateBlockers::GetInstance();
        if (blockers == nullptr) {
            return false;
        }

        // The service does no reference counting, so the flag above is what
        // keeps a second hide from taking a second push.
        if (visible) {
            blockers->Unblock(SDK::wh::guimodule::E_UIState::BlockCompass);
        }
        else {
            blockers->Block(SDK::wh::guimodule::E_UIState::BlockCompass);
        }

        _compassBlocked = !visible;
        return true;
    }

    bool MapService::GetWaypoint(Waypoint &waypoint) const {
        SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        if (map == nullptr) {
            return false;
        }

        const SDK::wh::guimodule::CheckpointMarkRef mark       = map->FindCheckpointForCurrentMap();
        const SDK::wh::guimodule::C_CheckpointMark *checkpoint = mark.Get();
        if (checkpoint == nullptr) {
            return false;
        }

        const SDK::Vec3 &position = checkpoint->GetWorldPosition();
        waypoint.position         = glm::vec3 {position.x, position.y, position.z};
        waypoint.mapId            = checkpoint->GetMapId();
        return true;
    }

    bool MapService::MoveWaypoint(const glm::vec3 &position) {
        SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        if (map == nullptr) {
            return false;
        }

        SDK::wh::guimodule::CheckpointMarkRef mark       = map->FindCheckpointForCurrentMap();
        SDK::wh::guimodule::C_CheckpointMark *checkpoint = mark.Get();
        if (checkpoint == nullptr) {
            return false;
        }

        // The compass re-reads the Vec3 every frame; the drawn pin does not.
        checkpoint->SetWorldPosition(SDK::Vec3 {position.x, position.y, position.z});
        map->SyncCheckpointToFlash();
        return true;
    }

    bool MapService::ClearWaypoint() {
        SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        if (map == nullptr || !map->HasCheckpoint()) {
            return false;
        }

        map->RemoveCheckpoint();
        return true;
    }

    bool MapService::IsKnownMarkType(const std::string &typeName) {
        SDK::wh::guimodule::E_CompassMarkType parsed = SDK::wh::guimodule::E_CompassMarkType::GeneralPoi;
        return SDK::wh::guimodule::CompassMarkTypeFromIconName(typeName.c_str(), parsed);
    }

    bool MapService::IsMarkTypeVisible(const std::string &typeName) const {
        SDK::wh::guimodule::E_CompassMarkType type = SDK::wh::guimodule::E_CompassMarkType::GeneralPoi;
        if (!SDK::wh::guimodule::CompassMarkTypeFromIconName(typeName.c_str(), type)) {
            return false;
        }

        SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        return map != nullptr && map->IsMarkTypeVisible(type);
    }

    bool MapService::SetMarkTypeVisible(const std::string &typeName, bool visible) {
        SDK::wh::guimodule::E_CompassMarkType type = SDK::wh::guimodule::E_CompassMarkType::GeneralPoi;
        if (!SDK::wh::guimodule::CompassMarkTypeFromIconName(typeName.c_str(), type)) {
            return false;
        }

        SDK::wh::guimodule::C_UIMap *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        if (map == nullptr) {
            return false;
        }

        map->SetMarkTypeVisible(type, visible);
        return true;
    }

    bool MapService::Discover(std::uint32_t data1, std::uint16_t data2, std::uint16_t data3, const std::uint8_t (&data4)[8]) const {
        SDK::wh::C_GlobalEnvironment *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        SDK::wh::rpgmodule::C_RPGModule *rpg      = environment != nullptr ? environment->GetRPGModule() : nullptr;
        if (rpg == nullptr) {
            return false;
        }

        SDK::wh::rpgmodule::S_LocationId id {data1, data2, data3, {}};
        std::memcpy(id.data4, data4, sizeof(id.data4));

        // The write the reveal makes: notify off, so no toast, trigger or XP.
        // It runs through the setter's detour, which raises poiDiscovered.
        SDK::wh::rpgmodule::C_RPGLocationManager *locations = rpg->GetLocationManager();
        if (SDK::wh::rpgmodule::I_POI *poi = locations->FindPOIById(id)) {
            poi->SetDiscoveryStatusAndNotify(static_cast<std::uint32_t>(SDK::wh::rpgmodule::E_DiscoveryStatus::Discovered), false);
            return true;
        }
        if (SDK::wh::rpgmodule::I_RPGLocationDiscovery *location = locations->FindLocationById(id)) {
            location->SetDiscoveryStatus(SDK::wh::rpgmodule::E_DiscoveryStatus::Discovered);
            return true;
        }
        return false;
    }

    bool MapService::RevealFog(const glm::vec3 &position) const {
        SDK::wh::C_GlobalEnvironment *environment     = SDK::wh::C_GlobalEnvironment::GetInstance();
        SDK::wh::playermodule::C_PlayerModule *player = environment != nullptr ? environment->GetPlayerModule() : nullptr;
        if (player == nullptr) {
            return false;
        }

        // The call C_FOWManager::RevealAt makes as the player walks.
        player->GetFOWManager()->GetFogOfWar()->Reveal(SDK::Vec2 {position.x, position.y}, true);
        return true;
    }

    bool MapService::RevealAllFog() {
        SDK::wh::C_GlobalEnvironment *environment     = SDK::wh::C_GlobalEnvironment::GetInstance();
        SDK::wh::playermodule::C_PlayerModule *player = environment != nullptr ? environment->GetPlayerModule() : nullptr;
        if (player == nullptr) {
            return false;
        }

        player->GetFOWManager()->GetFogOfWar()->RevealAll();
        _allFogRevealed = true;
        return true;
    }

    bool MapService::IsLabelShown(SDK::wh::rpgmodule::I_RPGLocationDiscovery *location) const {
        return !_allFogRevealed || location->GetDiscoveryStatus() != SDK::wh::rpgmodule::E_DiscoveryStatus::Undiscovered;
    }

    bool MapService::IsTraveling() const {
        SDK::wh::playermodule::C_FastTravel *travel = SDK::wh::playermodule::C_FastTravel::GetInstance();
        return travel != nullptr && travel->IsTraveling();
    }

    glm::vec3 MapService::GetTravelPosition() const {
        SDK::wh::playermodule::C_FastTravel *travel = SDK::wh::playermodule::C_FastTravel::GetInstance();
        if (travel == nullptr || !travel->IsTraveling()) {
            return glm::vec3 {0.0F};
        }

        const SDK::Vec3 &position = travel->GetCurrentTravelPosition();
        return glm::vec3 {position.x, position.y, position.z};
    }

    std::optional<std::uint8_t> MapService::ValidateTravelStart() const {
        SDK::wh::playermodule::C_FastTravel *travel = SDK::wh::playermodule::C_FastTravel::GetInstance();
        if (travel == nullptr) {
            return std::nullopt;
        }

        return static_cast<std::uint8_t>(travel->ValidateTravelStart());
    }

    void MapService::Reset() {
        SetCompassVisible(true);
        _allFogRevealed = false;
    }

    void MapService::NativeContextChanged() {
        _allFogRevealed = false;
    }
} // namespace KCDC::Features::Map
