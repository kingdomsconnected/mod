#include "navmesh_codec.h"

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace KCDC::Features::Navigation {
    namespace {
        /** Little-endian reads over a span that remember whether any of them ran off its end. */
        class Reader final {
          public:
            explicit Reader(std::span<const std::byte> bytes): _bytes(bytes) {}

            template <typename T>
            T Read() {
                T value {};
                if (_cursor + sizeof(T) > _bytes.size()) {
                    _overran = true;
                    return value;
                }
                std::memcpy(&value, _bytes.data() + _cursor, sizeof(T));
                _cursor += sizeof(T);
                return value;
            }

            bool Overran() const {
                return _overran;
            }

          private:
            std::span<const std::byte> _bytes;
            std::size_t _cursor = 0;
            bool _overran       = false;
        };

        /** The layer header as the game serializes it: 17 ints, then 11 floats. */
        struct GameHeader {
            std::int32_t magic           = 0;
            std::int32_t version         = 0;
            std::int32_t x               = 0;
            std::int32_t y               = 0;
            std::int32_t layer           = 0;
            std::int32_t userId          = 0;
            std::int32_t polyCount       = 0;
            std::int32_t vertCount       = 0;
            std::int32_t packedVertCount = 0;
            // Sizes the game's own link pool -- 4-byte links with 12-bit indices -- not Detour's.
            std::int32_t maxLinkCount = 0;
            // Sizes a 10-byte-per-entry runtime block the game builds on load; nothing of it is on disk.
            std::int32_t runtimeCount    = 0;
            std::int32_t detailMeshCount = 0;
            std::int32_t detailVertCount = 0;
            std::int32_t detailTriCount  = 0;
            std::int32_t bvNodeCount     = 0;
            std::int32_t offMeshConCount = 0;
            std::int32_t offMeshBase     = 0;
            float walkableHeight         = 0.0F;
            float walkableRadius         = 0.0F;
            float walkableClimb          = 0.0F;
            // 0.8 on every shipped layer; not proven to mean anything Detour has.
            float unknown50     = 0.0F;
            float bmin[3]       = {};
            float bmax[3]       = {};
            float bvQuantFactor = 0.0F;
        };

        constexpr std::size_t kPackedVertSize = 4;
        constexpr std::size_t kFloatVertSize  = 12;
        constexpr std::size_t kPolySize       = 28;
        constexpr std::size_t kOffMeshSize    = 52;

        /** The poly type byte the game writes; Detour keeps the same two in the top bits of `areaAndtype`. */
        constexpr std::uint8_t kGamePolyGround  = 0;
        constexpr std::uint8_t kGamePolyOffMesh = 1;

        /** Bit 0 of an off-mesh connection's flags: it can be crossed both ways, as a door can. */
        constexpr std::uint8_t kGameOffMeshBidirectional = 0x01;

        GameHeader ReadHeader(Reader &reader) {
            GameHeader header;
            header.magic           = reader.Read<std::int32_t>();
            header.version         = reader.Read<std::int32_t>();
            header.x               = reader.Read<std::int32_t>();
            header.y               = reader.Read<std::int32_t>();
            header.layer           = reader.Read<std::int32_t>();
            header.userId          = reader.Read<std::int32_t>();
            header.polyCount       = reader.Read<std::int32_t>();
            header.vertCount       = reader.Read<std::int32_t>();
            header.packedVertCount = reader.Read<std::int32_t>();
            header.maxLinkCount    = reader.Read<std::int32_t>();
            header.runtimeCount    = reader.Read<std::int32_t>();
            header.detailMeshCount = reader.Read<std::int32_t>();
            header.detailVertCount = reader.Read<std::int32_t>();
            header.detailTriCount  = reader.Read<std::int32_t>();
            header.bvNodeCount     = reader.Read<std::int32_t>();
            header.offMeshConCount = reader.Read<std::int32_t>();
            header.offMeshBase     = reader.Read<std::int32_t>();
            header.walkableHeight  = reader.Read<float>();
            header.walkableRadius  = reader.Read<float>();
            header.walkableClimb   = reader.Read<float>();
            header.unknown50       = reader.Read<float>();
            for (float &value : header.bmin) {
                value = reader.Read<float>();
            }
            for (float &value : header.bmax) {
                value = reader.Read<float>();
            }
            header.bvQuantFactor = reader.Read<float>();
            return header;
        }

        /** Why the header cannot be read as a tile, or empty. */
        std::string CheckHeader(const GameHeader &header, std::size_t size) {
            if (header.magic != DT_NAVMESH_MAGIC) {
                return "not a Detour tile";
            }
            if (header.version != TileCodec::kGameTileVersion) {
                return "tile version " + std::to_string(header.version) + ", expected " + std::to_string(TileCodec::kGameTileVersion);
            }
            if (header.polyCount < 0 || header.polyCount > 0xFFFF || header.vertCount < 0 || header.vertCount > 0xFFFF || header.packedVertCount < 0 || header.packedVertCount > header.vertCount) {
                return "counts out of range";
            }
            if (header.offMeshConCount < 0 || header.offMeshBase < 0 || static_cast<std::int64_t>(header.offMeshBase) + header.offMeshConCount != header.polyCount) {
                return "off-mesh polygons do not close the polygon list";
            }
            for (int axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(header.bmin[axis]) || !std::isfinite(header.bmax[axis]) || header.bmin[axis] > header.bmax[axis]) {
                    return "bounds are not finite and ordered";
                }
            }
            // The tile grid is sized from these; a tile with no width cannot place one.
            if (!(header.bmax[0] > header.bmin[0]) || !(header.bmax[1] > header.bmin[1])) {
                return "the tile has no area";
            }
            // The game reads these through a path no shipped tile takes, so their layout is unproven.
            if (header.detailMeshCount != 0 || header.detailVertCount != 0 || header.detailTriCount != 0 || header.bvNodeCount != 0) {
                return "tile carries detail meshes or a BV tree";
            }
            const std::size_t expected = TileCodec::kHeaderSize + kPackedVertSize * static_cast<std::size_t>(header.packedVertCount) + kFloatVertSize * static_cast<std::size_t>(header.vertCount - header.packedVertCount) + kPolySize * static_cast<std::size_t>(header.polyCount)
                                         + kOffMeshSize * static_cast<std::size_t>(header.offMeshConCount);
            if (expected != size) {
                return "size " + std::to_string(size) + " does not match its counts (" + std::to_string(expected) + ")";
            }
            return {};
        }

        /**
         * A section of the rebuilt tile, rounded up as `dtCreateNavMeshData`
         * rounds it. CheckHeader caps polygons and vertices at 0xFFFF, which
         * keeps every section, and the tile they add up to, far inside an int.
         */
        int SectionSize(std::size_t elementSize, int count) {
            return dtAlign4(static_cast<int>(elementSize * static_cast<std::size_t>(count)));
        }

        bool IsFinite(const float *values, std::size_t count) {
            return std::all_of(values, values + count, [](float value) {
                return std::isfinite(value);
            });
        }
    } // namespace

    std::optional<std::vector<TileRecord>> TileCodec::ParseIndex(std::span<const std::byte> index, std::size_t tilesSize) {
        if (index.size() % sizeof(std::int32_t) != 0) {
            return std::nullopt;
        }

        Reader reader(index);
        const std::size_t words = index.size() / sizeof(std::int32_t);
        std::size_t consumed    = 0;
        std::vector<TileRecord> records;
        while (consumed < words) {
            const auto x      = reader.Read<std::int32_t>();
            const auto y      = reader.Read<std::int32_t>();
            auto offset       = static_cast<std::uint64_t>(reader.Read<std::uint32_t>());
            const auto layers = reader.Read<std::int32_t>();
            consumed += 4;
            // No shipped column stacks more than six layers; a count far past that is not an index.
            if (reader.Overran() || layers < 1 || layers > 255 || consumed + static_cast<std::size_t>(layers) > words) {
                return std::nullopt;
            }
            for (std::int32_t layer = 0; layer < layers; ++layer) {
                const auto size = reader.Read<std::uint32_t>();
                if (size < kHeaderSize || offset + size > tilesSize) {
                    return std::nullopt;
                }
                records.push_back(TileRecord {x, y, static_cast<std::uint32_t>(offset), size});
                offset += size;
            }
            consumed += static_cast<std::size_t>(layers);
        }
        return records;
    }

    bool TileCodec::Decode(std::span<const std::byte> tile, const fu2::function<std::uint32_t(const EntityLink &) const> &assignId, DecodedTile &out, std::string &error) {
        Reader reader(tile);
        const GameHeader header = ReadHeader(reader);
        if (reader.Overran()) {
            error = "truncated header";
            return false;
        }
        error = CheckHeader(header, tile.size());
        if (!error.empty()) {
            return false;
        }

        const int groundCount = header.offMeshBase;

        // Game-space vertices first; the polygons below are checked against them.
        std::vector<float> verts(static_cast<std::size_t>(header.vertCount) * 3);
        const float extent[3] = {header.bmax[0] - header.bmin[0], header.bmax[1] - header.bmin[1], header.bmax[2] - header.bmin[2]};
        for (std::int32_t i = 0; i < header.vertCount; ++i) {
            float game[3] = {};
            if (i < header.packedVertCount) {
                // X in the top 11 bits and Y in the next 11, across the layer's bounds; height in the low 10.
                const auto packed = reader.Read<std::uint32_t>();
                game[0]           = header.bmin[0] + static_cast<float>(packed >> 21) * extent[0] / 2047.0F;
                game[1]           = header.bmin[1] + static_cast<float>((packed >> 10) & 0x7FFU) * extent[1] / 2047.0F;
                game[2]           = header.bmin[2] + static_cast<float>(packed & 0x3FFU) * extent[2] / 1023.0F;
            }
            else {
                for (float &value : game) {
                    value = reader.Read<float>();
                }
            }
            if (!IsFinite(game, 3)) {
                error = "vertex " + std::to_string(i) + " is not finite";
                return false;
            }
            ToDetour(game, &verts[static_cast<std::size_t>(i) * 3]);
        }

        // Polygons, as Detour stores them less the link list it builds on load.
        std::vector<dtPoly> polys(static_cast<std::size_t>(header.polyCount));
        int edgeCount   = 0;
        int portalCount = 0;
        int triCount    = 0;
        for (std::int32_t i = 0; i < header.polyCount; ++i) {
            dtPoly &poly = polys[static_cast<std::size_t>(i)];
            for (unsigned short &vert : poly.verts) {
                vert = reader.Read<std::uint16_t>();
            }
            for (unsigned short &nei : poly.neis) {
                nei = reader.Read<std::uint16_t>();
            }
            poly.firstLink     = DT_NULL_LINK;
            poly.flags         = reader.Read<std::uint8_t>();
            poly.vertCount     = reader.Read<std::uint8_t>();
            const auto type    = reader.Read<std::uint8_t>();
            const auto area    = reader.Read<std::uint8_t>();
            const bool offMesh = i >= header.offMeshBase;
            const int minVerts = offMesh ? 2 : 3;
            const int maxVerts = offMesh ? 2 : DT_VERTS_PER_POLYGON;
            if (type != (offMesh ? kGamePolyOffMesh : kGamePolyGround) || poly.vertCount < minVerts || poly.vertCount > maxVerts || area >= DT_MAX_AREAS) {
                error = "polygon " + std::to_string(i) + " is malformed";
                return false;
            }
            for (int j = 0; j < poly.vertCount; ++j) {
                if (poly.verts[j] >= header.vertCount) {
                    error = "polygon " + std::to_string(i) + " names a vertex the tile does not have";
                    return false;
                }
                const unsigned short nei = poly.neis[j];
                if ((nei & DT_EXT_LINK) == 0 && nei > groundCount) {
                    error = "polygon " + std::to_string(i) + " names a neighbour the tile does not have";
                    return false;
                }
            }
            poly.setArea(area);
            poly.setType(offMesh ? DT_POLYTYPE_OFFMESH_CONNECTION : DT_POLYTYPE_GROUND);
            if (!offMesh) {
                edgeCount += poly.vertCount;
                triCount += poly.vertCount - 2;
                for (int j = 0; j < poly.vertCount; ++j) {
                    portalCount += (poly.neis[j] & DT_EXT_LINK) != 0 ? 1 : 0;
                }
            }
        }

        std::vector<dtOffMeshConnection> offMeshCons(static_cast<std::size_t>(header.offMeshConCount));
        for (std::int32_t i = 0; i < header.offMeshConCount; ++i) {
            dtOffMeshConnection &con = offMeshCons[static_cast<std::size_t>(i)];
            float game[6]            = {};
            for (float &value : game) {
                value = reader.Read<float>();
            }
            ToDetour(&game[0], &con.pos[0]);
            ToDetour(&game[3], &con.pos[3]);
            con.rad               = reader.Read<float>();
            con.poly              = reader.Read<std::uint16_t>();
            const auto flags      = reader.Read<std::uint8_t>();
            con.side              = reader.Read<std::uint8_t>();
            const auto entityGuid = reader.Read<std::uint64_t>();
            const auto kind       = reader.Read<std::int32_t>();
            for (int skipped = 0; skipped < 4; ++skipped) {
                reader.Read<std::uint16_t>();
            }
            con.flags = (flags & kGameOffMeshBidirectional) != 0 ? DT_OFFMESH_CON_BIDIR : 0;
            // Every shipped tile pairs connection i with polygon offMeshBase + i, and the query filter reads a polygon's connection by that index.
            if (con.poly != header.offMeshBase + i) {
                error = "off-mesh connection " + std::to_string(i) + " is not paired with its polygon";
                return false;
            }
            if (!IsFinite(game, 6) || !std::isfinite(con.rad)) {
                error = "off-mesh connection " + std::to_string(i) + " is not finite";
                return false;
            }
            con.userId = entityGuid != 0 ? assignId(EntityLink {entityGuid, kind}) : 0;
        }
        if (reader.Overran()) {
            error = "truncated body";
            return false;
        }

        // dtCreateNavMeshData's estimate -- every edge, both halves of every portal, both ends of every
        // off-mesh link -- plus slack for links arriving from connections that start in a neighbour,
        // which this tile cannot count. The game's own maxLinkCount sizes a different pool (4-byte
        // links, 12-bit indices) and is never trusted here.
        const int maxLinkCount = edgeCount + portalCount * 2 + header.offMeshConCount * 4 + 16;

        const int headerSize = dtAlign4(static_cast<int>(sizeof(dtMeshHeader)));
        const int polysSize  = SectionSize(sizeof(dtPoly), header.polyCount);

        // Detour lays sections out on 4-byte boundaries, but a 64-bit dtLink wants 8, and addTile
        // derives every section from the counts in the header. Unused vertices at the end of the
        // list are the one thing that can move the link section without changing the tile.
        int storedVertCount = header.vertCount;
        while ((headerSize + SectionSize(sizeof(float) * 3, storedVertCount) + polysSize) % static_cast<int>(alignof(dtLink)) != 0) {
            ++storedVertCount;
        }
        const int vertsSize        = SectionSize(sizeof(float) * 3, storedVertCount);
        const int linksSize        = SectionSize(sizeof(dtLink), maxLinkCount);
        const int detailMeshesSize = SectionSize(sizeof(dtPolyDetail), groundCount);
        const int detailTrisSize   = SectionSize(4, triCount);
        const int offMeshConsSize  = SectionSize(sizeof(dtOffMeshConnection), header.offMeshConCount);
        const std::size_t total    = static_cast<std::size_t>(headerSize) + vertsSize + polysSize + linksSize + detailMeshesSize + detailTrisSize + offMeshConsSize;
        if (total > static_cast<std::size_t>(INT_MAX)) {
            error = "the rebuilt tile would be too large";
            return false;
        }
        const int dataSize = static_cast<int>(total);

        auto *data = static_cast<unsigned char *>(dtAlloc(static_cast<std::size_t>(dataSize), DT_ALLOC_PERM));
        if (data == nullptr) {
            error = "out of memory";
            return false;
        }
        std::memset(data, 0, static_cast<std::size_t>(dataSize));

        unsigned char *cursor = data;
        auto *outHeader       = reinterpret_cast<dtMeshHeader *>(cursor);
        cursor += headerSize;
        auto *outVerts = reinterpret_cast<float *>(cursor);
        cursor += vertsSize;
        auto *outPolys = reinterpret_cast<dtPoly *>(cursor);
        cursor += polysSize + linksSize;
        auto *outDetailMeshes = reinterpret_cast<dtPolyDetail *>(cursor);
        cursor += detailMeshesSize;
        auto *outDetailTris = cursor;
        cursor += detailTrisSize;
        auto *outOffMeshCons = reinterpret_cast<dtOffMeshConnection *>(cursor);

        outHeader->magic           = DT_NAVMESH_MAGIC;
        outHeader->version         = DT_NAVMESH_VERSION;
        outHeader->x               = header.x;
        outHeader->y               = header.y;
        outHeader->layer           = header.layer;
        outHeader->userId          = 0;
        outHeader->polyCount       = header.polyCount;
        outHeader->vertCount       = storedVertCount;
        outHeader->maxLinkCount    = maxLinkCount;
        outHeader->detailMeshCount = groundCount;
        outHeader->detailVertCount = 0;
        outHeader->detailTriCount  = triCount;
        outHeader->bvNodeCount     = 0;
        outHeader->offMeshConCount = header.offMeshConCount;
        outHeader->offMeshBase     = header.offMeshBase;
        outHeader->walkableHeight  = header.walkableHeight;
        outHeader->walkableRadius  = header.walkableRadius;
        outHeader->walkableClimb   = header.walkableClimb;
        ToDetour(header.bmin, outHeader->bmin);
        ToDetour(header.bmax, outHeader->bmax);
        outHeader->bvQuantFactor = header.bvQuantFactor;

        std::copy(verts.begin(), verts.end(), outVerts);
        std::copy(polys.begin(), polys.end(), outPolys);
        std::copy(offMeshCons.begin(), offMeshCons.end(), outOffMeshCons);

        // No detail mesh ships, and Detour's height queries read one for every ground polygon:
        // a fan over the polygon's own vertices, as dtCreateNavMeshData builds when given none.
        int triBase = 0;
        for (int i = 0; i < groundCount; ++i) {
            const int vertCount  = outPolys[i].vertCount;
            dtPolyDetail &detail = outDetailMeshes[i];
            detail.vertBase      = 0;
            detail.vertCount     = 0;
            detail.triBase       = static_cast<unsigned int>(triBase);
            detail.triCount      = static_cast<unsigned char>(vertCount - 2);
            for (int j = 2; j < vertCount; ++j) {
                unsigned char *tri = &outDetailTris[static_cast<std::size_t>(triBase) * 4];
                tri[0]             = 0;
                tri[1]             = static_cast<unsigned char>(j - 1);
                tri[2]             = static_cast<unsigned char>(j);
                // One bit per triangle edge that lies on the polygon's boundary.
                tri[3] = 1 << 2;
                if (j == 2) {
                    tri[3] |= 1 << 0;
                }
                if (j == vertCount - 1) {
                    tri[3] |= 1 << 4;
                }
                ++triBase;
            }
        }

        out.data = data;
        out.size = dataSize;
        return true;
    }

    void TileCodec::ToDetour(const float *game, float *detour) {
        const float x = game[0];
        const float y = game[1];
        const float z = game[2];
        detour[0]     = x;
        detour[1]     = z;
        detour[2]     = y;
    }

    void TileCodec::ToGame(const float *detour, float *game) {
        const float x = detour[0];
        const float y = detour[1];
        const float z = detour[2];
        game[0]       = x;
        game[1]       = z;
        game[2]       = y;
    }
} // namespace KCDC::Features::Navigation
