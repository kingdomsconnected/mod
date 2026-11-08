#pragma once

#include "navmesh.h"

#include "features/door/door_service.h"
#include "shared/features/door/door_entity.h"

#include <mafianet/virtual_world.h>

#include <cstdint>
#include <function2/function2.hpp>
#include <map>
#include <string>

namespace KCDC::Features::Navigation {
    /** Which doors a query walks through. Links that are not doors are crossed under every policy. */
    enum class DoorPolicy : std::uint8_t {
        /** Unlocked doors, open or closed. */
        Unlocked,
        /** Every door, locked or not. */
        Any,
        /** No doorways. */
        None,
        /** Only doors that stand open right now. */
        Open,
    };

    /**
     * The level's navigation mesh, when the server has been given one.
     *
     * Load `mod.navmesh` first, then search the server's files directory.
     * The game data must be supplied separately. If no usable copy is found,
     * dormant NPCs in auto mode move in straight lines.
     */
    class NavigationService final {
      public:
        /** Loads a configured or discovered mesh. Never fails the server. Needs the doors installed first. */
        void Install(const std::string &level, const std::string &source, const KCDC::Features::Door::DoorService &doors) {
            // A door the level places lives in the global world from boot; its state is read off that one.
            Install(level, source, [&doors](std::uint64_t doorGuid) -> const KCDC::Shared::Entities::DoorEntity * {
                return doors.Find(doorGuid, MafiaNet::VIRTUAL_WORLD_GLOBAL);
            });
        }

        /** As above, with the door each link names looked up through `findDoor`, which answers null for a door the server does not know. */
        void Install(const std::string &level, const std::string &source, fu2::function<const KCDC::Shared::Entities::DoorEntity *(std::uint64_t) const> findDoor);

        bool IsReady() const {
            return _mesh.IsReady();
        }

        /** The `recast.pak` the mesh came from, or empty when there is none. */
        const std::string &GetSourcePath() const {
            return _sourcePath;
        }

        Navmesh &GetMesh() {
            return _mesh;
        }

        const Navmesh &GetMesh() const {
            return _mesh;
        }

        /** Enables an overlay, reading its tiles out of the pak. False for a name the mesh refuses; a failure to read or load it is logged. */
        bool EnableOverlay(const std::string &name);

        /** Disables an overlay. False for a name that is not enabled; a column that could not be put back is logged. */
        bool DisableOverlay(const std::string &name);

        /** Search distances, a door gate owned by this service, and an A* budget for one query. */
        QueryOptions Options(DoorPolicy policy, float searchRadius, float searchHeight, int maxSearchNodes = Navmesh::kMaxSearchNodes) const;

        /** The defaults: unlocked doors, the mesh within 2 m across and 4 m up or down, and the full search budget. */
        QueryOptions DefaultOptions() const {
            return Options(DoorPolicy::Unlocked, 2.0F, 4.0F);
        }

        /**
         * Whether a query under `policy` may cross `link`, given the door it
         * names, or null when the server does not know that door. A door the
         * server does not know is not gated, and neither is a link that is
         * not a door.
         */
        static bool MayCross(DoorPolicy policy, const EntityLink &link, const KCDC::Shared::Entities::DoorEntity *door);

        /**
         * Pulls the base mesh and every overlay's index out of the pak; an
         * overlay's tiles are left in it and only their length is recorded.
         * An overlay missing half its pair is dropped. False, with `error`
         * set, when the base mesh is not in it.
         */
        static bool ReadPak(const std::string &path, MeshFiles &base, std::map<std::string, OverlayIndex> &overlays, std::string &error);

        /**
         * Where each of a level's catalogued doors stands when shut, for the
         * doors the mesh marks no doorway for (`Navmesh::SetDoorLeaves`).
         *
         * The catalog carries a door's pivot and facing but not its leaf. The
         * game's own door links say what it is: across all 995 kutnohorsko
         * doors the mesh links, the middle of the link lies exactly
         * `kLeafWidth / 2` from the pivot along the door's side axis -- towards
         * +side for a door whose model is named `left`, -side for `right`. A
         * door named neither is given the leaf both ways.
         */
        static std::vector<Navmesh::DoorLeaf> DoorLeaves(const std::string &level);

        /** A door leaf's width, from the game's own door links; see `DoorLeaves`. */
        static constexpr float kLeafWidth = Navmesh::kDoorLeafWidth;

      private:
        Navmesh _mesh;
        std::string _sourcePath;
        fu2::function<const KCDC::Shared::Entities::DoorEntity *(std::uint64_t) const> _findDoor;

        fu2::function<bool(const EntityLink &) const> _unlockedDoors;
        fu2::function<bool(const EntityLink &) const> _openDoors;
        fu2::function<bool(const EntityLink &) const> _noDoors;
    };
} // namespace KCDC::Features::Navigation
