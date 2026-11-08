#pragma once

#include "shared/features/area/area_definition.h"
#include "shared/game/area_catalog.h"

#include <mafianet/virtual_world.h>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Area {
    /** What kind of body is standing in an area; decides which handle a script is given for it. */
    enum class AreaOccupantKind : std::uint8_t {
        Player,
        Horse,
        Cart,
        Npc,
    };

    /**
     * One area the server tests bodies against: one of the level's own, or
     * one a script or the World Builder drew.
     *
     * A level area is read-only geometry out of the compiled area catalog; a
     * script area owns an `AreaShape`. Both are tested by the engine's own
     * rule for their kind (see area_geometry.h).
     */
    struct AreaRecord {
        std::uint64_t collisionNetworkId = 0;
        std::uint32_t handle = 0; // what a script handle holds; never reused

        /** Null for a script area. */
        const KCDC::Shared::Game::LevelArea *level = nullptr;

        /** A union's members, by handle. */
        std::vector<std::uint32_t> members;

        /**
         * What a script built, as `toJSON` writes it back. A level area fills
         * only the id (its GUID as hex), name, labels and `enabled`; its
         * shape is the catalog row, and it is in every world.
         */
        KCDC::Shared::Game::AreaDefinition definition;

        std::vector<glm::vec3> worldPoints; // a script shape's corners, kept in step with its shape

        /** Set while `destroy` raises the last exits, so a handler destroying it again does nothing. */
        bool destroying = false;

        glm::vec3 boundsMin {0.0F};
        glm::vec3 boundsMax {0.0F};

        /** Every body currently inside, by network ID. */
        std::unordered_map<std::uint64_t, AreaOccupantKind> occupants;

        bool IsLevel() const {
            return level != nullptr;
        }
    };

    /** A `crime_door` link from a level area, resolved against the door catalog. */
    struct AreaDoor {
        std::uint64_t doorGuid = 0;
        KCDC::Shared::Game::AreaDoorLink link = KCDC::Shared::Game::AreaDoorLink::Basic;
        std::string name;
        glm::vec3 position {0.0F};
    };

    /**
     * Server authority over areas: the index, the containment test, and the
     * enter/exit bookkeeping.
     *
     * The server keeps every body's pose already, so it decides who is inside
     * itself: a body is re-tested only when it
     * has moved or changed world, and a script area that is created, moved,
     * reshaped or switched re-tests everyone. Nothing is kept past the
     * session -- a script saves what it made through `toJSON` and builds it
     * again with `Area.create`.
     */
    class AreaService final {
      public:
        void Install(const std::string &level);

        /** Re-tests whatever moved, and raises the crossings. Server tick. */
        void Update();

        AreaRecord *Find(std::uint32_t handle);
        const AreaRecord *Find(std::uint32_t handle) const;

        /** A level area by its GUID hex, or a script area by its id. */
        AreaRecord *FindById(const std::string &id);

        /**
         * Builds a script area. Null, with `error` set, for an id already taken
         * or spelt like a level GUID, or a shape the engine could not build.
         */
        AreaRecord *Create(KCDC::Shared::Game::AreaDefinition definition, std::string &error);

        /**
         * Whether a body in `bodyWorld` shares an area's world. An area with
         * none is in every world, and so is a body in `VIRTUAL_WORLD_GLOBAL`,
         * which MafiaNet streams everywhere -- where every player starts.
         */
        static bool SharesWorld(const AreaRecord &area, MafiaNet::VirtualWorldId bodyWorld);

        /** Removes a script area, raising `areaExit` for whoever was in it first. False for a level area or a stale handle. */
        bool Destroy(std::uint32_t handle);

        /** Replaces a script area's shape; false, and nothing changes, when the engine could not build it. */
        bool SetShape(AreaRecord &area, const KCDC::Shared::Game::AreaShape &shape);

        void SetEnabled(AreaRecord &area, bool enabled);
        bool SetCollision(AreaRecord &area, bool collision, std::string &error);
        /** Replaces how a script area's boundary is built; false, and nothing changes, when it could not be built. */
        bool SetCollisionOptions(AreaRecord &area, const KCDC::Shared::Game::AreaCollisionOptions &options, std::string &error);
        void SetVirtualWorld(AreaRecord &area, std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        /** Whether a point lies in an area, whatever its switch and world. */
        bool Contains(const AreaRecord &area, const glm::vec3 &point) const;

        /** Every enabled area containing a point; a script area in another world is left out when `virtualWorld` is given. */
        std::vector<AreaRecord *> At(const glm::vec3 &point, std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        /** Every area whose box comes within `radius` of a point, nearest box first. */
        std::vector<AreaRecord *> Nearby(const glm::vec3 &point, float radius, std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        std::vector<AreaRecord *> WithLabel(const std::string &label);

        /** Every area, level ones first; `level` narrows it to one kind. */
        std::vector<AreaRecord *> All(std::optional<bool> level);

        /** The `crime_door` links a level area carries; empty for a script area. */
        std::vector<AreaDoor> DoorsOf(const AreaRecord &area) const;

        /** Every level area that links a door. */
        std::vector<AreaRecord *> AreasForDoor(std::uint64_t doorGuid);

      private:
        /** One body the service follows. */
        struct Tracked {
            AreaOccupantKind kind = AreaOccupantKind::Player;
            glm::vec3 position {0.0F};
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            std::vector<std::uint32_t> inside;
            bool seen = false; // refreshed each tick; a body not seen has gone
        };

        struct Crossing {
            bool enter              = false;
            std::uint32_t handle    = 0;
            std::uint64_t networkId = 0;
            AreaOccupantKind kind   = AreaOccupantKind::Player;
            bool matchingWorld      = true;
        };

        static constexpr float kCellSize = 64.0F;

        static std::int64_t CellKey(std::int32_t x, std::int32_t y);

        AreaRecord &Insert(std::unique_ptr<AreaRecord> area);
        /** Calls `visit(cell)` for every grid cell an area's box covers; false, visiting none, for one too wide to grid. */
        template <typename Visit>
        static bool ForEachCell(const AreaRecord &area, Visit &&visit);

        void IndexArea(const AreaRecord &area);
        void UnindexArea(const AreaRecord &area);

        /** Recomputes a script area's world points and box after its shape changed. */
        static void RefreshGeometry(AreaRecord &area);
        bool SyncCollision(AreaRecord &area, std::string &error);
        void RemoveCollision(AreaRecord &area);

        /** Calls `visit(handle)` for every area whose box could hold a point: its grid cell, then every area too wide to grid. */
        template <typename Visit>
        void ForEachCandidate(const glm::vec3 &point, Visit &&visit) const;

        void Follow(std::uint64_t networkId, AreaOccupantKind kind, const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld);
        void Evaluate(std::uint64_t networkId, Tracked &tracked);

        static void Emit(const Crossing &crossing);

        /** Raises this tick's crossings in order; one whose area a handler destroyed has already gone out with it. */
        void RaisePending();

        std::string _level;
        std::unordered_map<std::uint32_t, std::unique_ptr<AreaRecord>> _areas;
        std::unordered_map<std::string, std::uint32_t> _byId;
        std::unordered_map<std::uint64_t, std::uint32_t> _byGuid;
        std::unordered_multimap<std::uint64_t, std::uint32_t> _byDoor;
        std::unordered_map<std::int64_t, std::vector<std::uint32_t>> _grid;

        /** Areas too wide to grid, tested for every body. */
        std::vector<std::uint32_t> _wide;
        std::uint32_t _nextHandle = 1;

        std::unordered_map<std::uint64_t, Tracked> _tracked;
        /** This tick's crossings, raised after the whole pass so a handler cannot change the index under it. */
        std::vector<Crossing> _pending;

        /** Set when an area changed: every body is re-tested on the next tick, moved or not. */
        bool _retestAll = true;
    };
} // namespace KCDC::Features::Area
