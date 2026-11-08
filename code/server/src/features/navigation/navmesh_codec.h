#pragma once

#include <cstddef>
#include <cstdint>
#include <function2/function2.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace KCDC::Features::Navigation {
    /**
     * Where one tile layer sits inside an `rdnavmesh_*.nav` file.
     *
     * The `.hdr` beside it is a flat list of `{tx, ty, offset, layerCount,
     * size[layerCount]}` records, one per tile column, with the column's layers
     * stored back to back from `offset`. This is that list flattened to one
     * entry per layer.
     */
    struct TileRecord {
        std::int32_t x       = 0;
        std::int32_t y       = 0;
        std::uint32_t offset = 0;
        std::uint32_t size   = 0;
    };

    /**
     * An off-mesh link that carries a level entity's GUID.
     *
     * `kind` is the connection's own type field: 8 on the links that cross a
     * door (every one of them names an AnimDoor from the door catalog), 3 on a
     * rarer kind that climbs about three metres and names no door or gate.
     *
     * The game's off-mesh reader (0x1807E571C) treats a connection as a
     * reference whenever its kind is not -1 and either the GUID is nonzero or
     * the secondary id built from its trailing four u16 is valid. Most links
     * are the second sort -- 19,695 of kutnohorsko's 21,189 carry GUID 0, a
     * valid secondary id and kind 0 -- and what that id resolves to is not
     * modeled. Only links with a nonzero GUID are reported; the rest are not
     * doors, and every query crosses them.
     */
    struct EntityLink {
        std::uint64_t entityGuid = 0;
        std::int32_t kind        = 0;
    };

    /**
     * One tile rebuilt in stock Detour's layout, in Detour's Y-up space.
     *
     * `data` comes from `dtAlloc`: hand it to `dtNavMesh::addTile` with
     * `DT_TILE_FREE_DATA`, or release it with `dtFree`.
     */
    struct DecodedTile {
        unsigned char *data = nullptr;
        int size            = 0;
    };

    /**
     * Reads the navigation mesh Warhorse ships in each level's `recast.pak`.
     *
     * The game runs a modified Detour whose tiles carry version 14 and are
     * serialized field by field (`ReadTile1807E5EB0`): a 112-byte
     * header, most vertices packed into 32 bits, 28-byte polygons and 52-byte
     * off-mesh connections, with no links, detail meshes or BV tree on disk,
     * all in the game's Z-up space. None of that is something stock Detour
     * can load, so each tile is rebuilt here into the layout
     * `dtCreateNavMeshData` would have written for the same polygons.
     */
    class TileCodec final {
      public:
        /** The version the game's `dtNavMesh::addTile` insists on. */
        static constexpr std::int32_t kGameTileVersion = 14;

        /** The `kind` of an off-mesh link that crosses a door. */
        static constexpr std::int32_t kDoorLinkKind = 8;

        /**
         * The polygon flag on a doorway. The ground runs through every doorway
         * whether or not its door is shut -- the door's off-mesh link is where
         * the game plays the swing, not the only way across -- so a shut door
         * is only honoured by refusing the polygon that carries this.
         */
        static constexpr unsigned short kDoorwayFlag = 0x04;

        /** The length of one layer's header on disk. */
        static constexpr std::size_t kHeaderSize = 112;

        /** Flattens a `.hdr` index. Empty when it is malformed or points outside `tilesSize` bytes of tile data. */
        static std::optional<std::vector<TileRecord>> ParseIndex(std::span<const std::byte> index, std::size_t tilesSize);

        /**
         * Rebuilds one tile layer. False, with `error` set and nothing
         * allocated, when the bytes are not a tile this reads.
         *
         * `assignId` is asked for the `userId` of every off-mesh connection
         * with a nonzero entity GUID; the rest carry 0.
         *
         * Every count, offset and float is checked before anything is sized
         * from it, so a hostile file is refused rather than trusted.
         */
        static bool Decode(std::span<const std::byte> tile, const fu2::function<std::uint32_t(const EntityLink &) const> &assignId, DecodedTile &out, std::string &error);

        /** Game space (Z-up) to Detour's Y-up: the same axes in a different order. */
        static void ToDetour(const float *game, float *detour);

        /** Detour's Y-up back to game space. */
        static void ToGame(const float *detour, float *game);
    };
} // namespace KCDC::Features::Navigation
