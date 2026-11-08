#pragma once

#include "navmesh_codec.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <function2/function2.hpp>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class dtNavMesh;
class dtNavMeshQuery;
struct dtMeshTile;
struct dtPoly;

namespace KCDC::Features::Navigation {
    /** One `rdnavmesh_*` pair out of a level's `recast.pak`: the `.hdr` index and the `.nav` it indexes. */
    struct MeshFiles {
        std::vector<std::byte> index;
        std::vector<std::byte> tiles;
    };

    /** An overlay's `.hdr`, and the length of the `.nav` it indexes, whose bytes are read only when the overlay is enabled. */
    struct OverlayIndex {
        std::vector<std::byte> index;
        std::uint64_t tilesSize = 0;
    };

    /** Where an overlay's `.nav` is read from when it is enabled. */
    class OverlayReader {
      public:
        virtual ~OverlayReader() = default;

        /** The overlay's tile bytes. False, with `error` set, when they cannot be read. */
        virtual bool ReadTiles(const std::string &name, std::vector<std::byte> &tiles, std::string &error) const = 0;
    };

    struct QueryOptions;

    /**
     * The game's navigation mesh for one level, in Detour.
     *
     * Everything in and out is in game space -- metres, Z up -- and only the
     * codec ever sees Detour's own axes. Not thread-safe: the server asks it
     * everything from the tick, and the queries share one Detour query and
     * one set of scratch buffers.
     *
     * The base mesh is the level as it ships. An overlay is one of the
     * `rdnavmesh_<name>_1` files beside it, the mesh a quest state leaves
     * behind (a cleared passage, a rebuilt bathhouse): each replaces every
     * layer of the tile columns it covers. Overlays stack in the order they
     * were enabled, the latest winning a column two of them cover, and
     * disabling one gives its columns back to whatever lies underneath.
     *
     * Only the overlays' indexes are read at load. An overlay's tiles are
     * read through the `OverlayReader`, and checked, when it is enabled; of
     * the base mesh only the columns some overlay covers stay in memory, to
     * be put back when the overlays over them are disabled.
     */
    class Navmesh final {
      public:
        struct PathDoor {
            std::uint64_t guid = 0;
            glm::vec3 position {0.0F};
        };

        struct Path {
            std::vector<glm::vec3> points;
            /** False when the goal could not be reached and the path ends as close to it as the mesh allows. */
            bool complete = false;
            /** Only doors crossed by this path, including ground-only and shared doorways. */
            std::vector<PathDoor> doors;
        };

        /**
         * Where a level door's leaf stands when shut, across the ground, at
         * the height of its threshold. For doors the mesh marks no doorway
         * for: see `SetDoorLeaves`.
         */
        struct DoorLeaf {
            std::uint64_t entityGuid = 0;
            glm::vec3 from {0.0F};
            glm::vec3 to {0.0F};
        };

        struct RayHit {
            /** Whether a wall stops the ray before its end. */
            bool hit = false;
            /** Where the ray stops: the wall, or the end. */
            glm::vec3 point {0.0F};
        };

        static bool ReadPak(const std::string &path, MeshFiles &base, std::map<std::string, OverlayIndex> &overlays, std::string &error);
        static std::vector<DoorLeaf> DoorLeaves(const std::string &level);
        bool LoadPak(const std::string &path, const std::string &level, std::string &error, std::vector<std::string> *rejectedOverlays = nullptr);
        static constexpr float kDoorLeafWidth = 1.2F;

        Navmesh();
        ~Navmesh();
        Navmesh(const Navmesh &)            = delete;
        Navmesh &operator=(const Navmesh &) = delete;

        /**
         * Replaces whatever was loaded. False, with `error` set and nothing
         * loaded, on a base mesh this cannot read. An overlay whose index this
         * cannot read is left out and named in `rejected`, with the reason,
         * rather than failing the level; one whose tiles are broken is found
         * when it is enabled.
         */
        bool Build(MeshFiles base, std::map<std::string, OverlayIndex> overlays, std::unique_ptr<OverlayReader> reader, std::vector<std::string> &rejected, std::string &error);

        /** As above, with every overlay's tiles already in memory. */
        bool Build(MeshFiles base, std::map<std::string, MeshFiles> overlays, std::vector<std::string> &rejected, std::string &error);

        /** Drops the mesh and every overlay. The door leaves stay, for the next `Build`. */
        void Clear();

        /**
         * The level's doors, for the ones the mesh itself names no doorway
         * for.
         *
         * A door the game's mesh knows has an off-mesh link through its
         * doorway, and the polygons there carry `TileCodec::kDoorwayFlag`. Some
         * catalogued doors have neither -- 137 of kutnohorsko's 1,132 -- and
         * the ground simply runs through them, so a query walked straight into
         * a shut leaf. Each such door is given a link of its own, keyed by its
         * GUID like any other, and the ground polygons its leaf crosses are
         * marked as its doorway, so every query's door policy applies to it.
         * Doors whose leaves cross the same polygons gate them together: they
         * are crossed only when every one of them may be.
         *
         * Takes effect at the next `Build` and is kept across overlays.
         */
        void SetDoorLeaves(std::vector<DoorLeaf> leaves);

        /** Doors given a doorway of their own at the last `Build`, and the polygons that marked. */
        std::size_t SyntheticDoorCount() const {
            return _syntheticDoorCount;
        }

        std::size_t SyntheticDoorwayCount() const {
            return _syntheticDoorways.size();
        }

        /** How far above or below a leaf's threshold a polygon may lie and still be the floor under it. */
        static constexpr float kLeafHeightReach = 1.0F;

        bool IsReady() const {
            return _mesh != nullptr;
        }

        std::size_t TileCount() const;
        std::size_t PolygonCount() const;

        /** Every overlay the level ships, by name, sorted. */
        std::vector<std::string> OverlayNames() const;

        /** The enabled overlays, oldest first. */
        const std::vector<std::string> &ActiveOverlays() const {
            return _active;
        }

        /**
         * False, with `error` empty, for a name the level does not ship or
         * one already enabled. False, with `error` set and the mesh as it
         * was, when the overlay's tiles cannot be read or Detour refuses one.
         */
        bool EnableOverlay(const std::string &name, std::string &error);

        /**
         * False, with `error` empty, for a name that is not enabled. False,
         * with `error` set, when the overlay is disabled but a column could
         * not be put back, which leaves that column without tiles.
         */
        bool DisableOverlay(const std::string &name, std::string &error);

        /** The nearest point on the mesh within the search distances of `point`. */
        std::optional<glm::vec3> ClosestPoint(const glm::vec3 &point, const QueryOptions &options) const;

        /** The height of the walkable surface nearest `point`, within the search distances. */
        std::optional<float> FloorAt(const glm::vec3 &point, const QueryOptions &options) const;

        /**
         * Straight-line corners from `from` to `to` along the mesh. Empty when
         * either end is off it. A search that runs out of
         * `options.maxSearchNodes` ends at the polygon nearest the goal it
         * reached, and the path is not complete.
         */
        std::optional<Path> FindPath(const glm::vec3 &from, const glm::vec3 &to, const QueryOptions &options) const;

        /** Walks the mesh surface from `from` towards `to`. Empty when `from` is off the mesh. */
        std::optional<RayHit> Raycast(const glm::vec3 &from, const glm::vec3 &to, const QueryOptions &options) const;

        /** A random point anywhere on the mesh: a uniformly chosen tile, then a point on it weighted by area. */
        std::optional<glm::vec3> RandomPoint(const QueryOptions &options) const;

        /**
         * A random point on the mesh reachable from `center` and, across the
         * ground, within `radius` of it. Detour draws from every polygon the
         * circle touches, so a draw past `radius` is thrown away and drawn
         * again, up to `kRandomPointAttempts` times; empty when none lands.
         */
        std::optional<glm::vec3> RandomPoint(const glm::vec3 &center, float radius, const QueryOptions &options) const;

        /** The most polygons one search may expand -- the query's node pool, and the default budget -- and the longest path it returns. */
        static constexpr int kMaxSearchNodes  = 65535;
        static constexpr int kMaxPathPolygons = 4096;

        /** Draws `RandomPoint` makes around a centre before it gives up. */
        static constexpr int kRandomPointAttempts = 32;

        /** Metres from a doorway polygon's centre that the middle of its door link may lie. */
        static constexpr float kDoorwayReach = 2.0F;

      private:
        struct Column {
            std::int32_t x                             = 0;
            std::int32_t y                             = 0;
            bool operator==(const Column &other) const = default;
        };

        struct ColumnHash {
            std::size_t operator()(const Column &column) const {
                return std::hash<std::uint64_t>()((static_cast<std::uint64_t>(static_cast<std::uint32_t>(column.x)) << 32) | static_cast<std::uint32_t>(column.y));
            }
        };

        /**
         * One mesh's tiles, grouped by the column they stand in. `tiles` holds
         * the bytes the records point into -- for the base mesh the columns an
         * overlay covers, for an overlay nothing -- and `tilesSize` is the
         * length of the `.nav` the index was checked against.
         */
        struct Source {
            std::vector<std::byte> tiles;
            std::uint64_t tilesSize = 0;
            std::unordered_map<Column, std::vector<TileRecord>, ColumnHash> columns;
        };

        class Filter;
        std::uint32_t LinkForPoly(std::uint64_t ref, const dtMeshTile &tile, const dtPoly &poly) const;
        void CollectPathDoors(std::span<const std::uint64_t> corridor, Path &path) const;

        /** Groups a mesh's tiles by the column they stand in. */
        static bool Index(std::span<const std::byte> index, std::uint64_t tilesSize, Source &out, std::string &error);

        /** Decodes every tile once and throws the result away, so a broken overlay is refused before it touches the mesh. */
        static bool Validate(const Source &source, std::span<const std::byte> tiles, std::string &error);

        /** Reads an overlay's tiles into `out`, checking they are as long as its index was told. */
        bool ReadOverlay(const std::string &name, std::vector<std::byte> &out, std::string &error) const;

        /** Decodes these tiles and adds them, appending each tile's reference to `added`. False on the first one Detour refuses. */
        bool AddTiles(std::span<const std::byte> tiles, const std::vector<TileRecord> &records, std::vector<std::uint64_t> &added, std::string &error);

        /** The `userId` a connection naming this entity carries: 1 + its row in `_links`. */
        std::uint32_t LinkId(const EntityLink &link);

        /** Ties each doorway polygon of a tile just added to the door link that crosses it, looking in the tiles around it. */
        void IndexDoorways(const dtMeshTile &tile);

        /** Forgets the doorways of a tile about to be removed, and takes back the flags `MarkDoorLeaves` set. */
        void ForgetDoorways(const dtMeshTile &tile);

        /** Marks the polygons of a tile that each linkless door's leaf crosses as that door's doorway. */
        void MarkDoorLeaves(const dtMeshTile &tile);

        /** Gives each door no link names a link of its own. Called once the base tiles have named theirs. */
        void RegisterDoorLeaves();

        /**
         * Puts each column back as the overlays stacked over the base mesh now
         * say it should be, then reindexes the doorways around all of them
         * once. `loaded` holds the overlay bytes read so far. False, with the
         * first error, when a column could not be filled; the rest still are.
         */
        bool RebuildColumns(const std::vector<Column> &columns, std::map<std::string, std::vector<std::byte>> &loaded, std::string &error);

        /** A uniform draw in [0, 1) for Detour's random point search, which takes a plain function. */
        static float RandomUnit();

        /** The polygon nearest `point` within the search distances, and the point on it; 0 when there is none. */
        std::uint64_t Nearest(const glm::vec3 &point, const QueryOptions &options, const Filter &filter, glm::vec3 &onMesh) const;

        std::unique_ptr<dtNavMesh, void (*)(dtNavMesh *)> _mesh;
        std::unique_ptr<dtNavMeshQuery, void (*)(dtNavMeshQuery *)> _query;
        Source _base;
        std::map<std::string, Source> _overlays;
        std::unique_ptr<OverlayReader> _reader;
        std::vector<std::string> _active;

        // One entry per level entity an off-mesh link names; a connection's userId is 1 + its index.
        std::vector<EntityLink> _links;
        std::unordered_map<std::uint64_t, std::uint32_t> _linkIds;

        // Doorway polygon reference to the userId of the door link crossing it.
        std::unordered_map<std::uint64_t, std::uint32_t> _doorways;

        // Doors the mesh names no doorway for, the userId each was given (0 when the mesh names it), the polygons tied to them, the
        // polygons whose doorway flag was set here rather than shipped, and the other doors whose leaves share a doorway with a door's.
        std::vector<DoorLeaf> _leaves;
        std::unordered_map<std::uint64_t, glm::vec3> _doorCenters;
        std::vector<std::uint32_t> _leafLinks;
        std::unordered_set<std::uint64_t> _syntheticDoorways;
        std::unordered_set<std::uint64_t> _leafFlags;
        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> _sharedDoorways;
        std::size_t _syntheticDoorCount = 0;

        // Scratch the queries reuse, sized at Build: a path corridor, the polygons a ray crosses, and path corners.
        mutable std::vector<std::uint64_t> _corridor;
        mutable std::vector<std::uint64_t> _visited;
        mutable std::vector<float> _corners;
    };

    /** What one query may use: how far from its points it looks for the mesh, which entity links it may cross, and how long it may search. */
    struct QueryOptions {
        /** Metres, across the ground and up or down, that a point may lie from the mesh and still be on it. */
        float searchRadius = 2.0F;
        float searchHeight = 4.0F;

        /**
         * Decides whether an off-mesh link that names a level entity may be
         * crossed; null crosses every link. Called during the search, so it
         * must be cheap and must not touch the mesh.
         */
        const fu2::function<bool(const EntityLink &) const> *linkGate = nullptr;

        /**
         * The most polygons `FindPath` may expand before it settles for the
         * nearest it reached; clamped to [1, `Navmesh::kMaxSearchNodes`].
         * Only path searches have a budget: the others look at a bounded
         * neighbourhood.
         */
        int maxSearchNodes = Navmesh::kMaxSearchNodes;
    };
} // namespace KCDC::Features::Navigation
