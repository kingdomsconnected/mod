#include "navmesh.h"

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <random>
#include <type_traits>
#include <unordered_set>

namespace KCDC::Features::Navigation {
    // The scratch buffers and Nearest hand polygon references out as plain 64-bit integers.
    static_assert(std::is_same_v<dtPolyRef, std::uint64_t>, "the navigation mesh is built with DT_POLYREF64");

    /**
     * Detour's default filter, plus the entity links: an off-mesh connection
     * that names a level entity is only crossed when the gate says so.
     */
    class Navmesh::Filter final: public dtQueryFilter {
      public:
        Filter(const Navmesh &navmesh, const fu2::function<bool(const EntityLink &) const> *gate): _navmesh(navmesh), _gate(gate) {}

        bool passFilter(const dtPolyRef ref, const dtMeshTile *tile, const dtPoly *poly) const override {
            if (!dtQueryFilter::passFilter(ref, tile, poly)) {
                return false;
            }
            if (_gate == nullptr) {
                return true;
            }
            const auto linkId = _navmesh.LinkForPoly(ref, *tile, *poly);
            if (linkId == 0 || linkId > _navmesh._links.size()) {
                return true;
            }
            if (!(*_gate)(_navmesh._links[linkId - 1])) {
                return false;
            }
            // Doors standing in one doorway: it is crossed only when every one of them may be.
            const auto shared = _navmesh._sharedDoorways.find(linkId);
            if (shared == _navmesh._sharedDoorways.end()) {
                return true;
            }
            return std::all_of(shared->second.begin(), shared->second.end(), [this](std::uint32_t other) {
                return (*_gate)(_navmesh._links[other - 1]);
            });
        }

      private:
        const Navmesh &_navmesh;
        const fu2::function<bool(const EntityLink &) const> *_gate;
    };

    namespace {
        void ToDetour(const glm::vec3 &game, float *detour) {
            const float values[3] = {game.x, game.y, game.z};
            TileCodec::ToDetour(values, detour);
        }

        glm::vec3 ToGame(const float *detour) {
            float game[3] = {};
            TileCodec::ToGame(detour, game);
            return glm::vec3(game[0], game[1], game[2]);
        }

        std::span<const std::byte> TileBytes(std::span<const std::byte> tiles, const TileRecord &record) {
            return tiles.subspan(record.offset, record.size);
        }

        /** Overlay tiles handed over in memory, as a caller that already holds them has them. */
        class MemoryOverlayReader final: public OverlayReader {
          public:
            explicit MemoryOverlayReader(std::map<std::string, std::vector<std::byte>> tiles): _tiles(std::move(tiles)) {}

            bool ReadTiles(const std::string &name, std::vector<std::byte> &tiles, std::string &error) const override {
                const auto found = _tiles.find(name);
                if (found == _tiles.end()) {
                    error = "no tiles for " + name;
                    return false;
                }
                tiles = found->second;
                return true;
            }

          private:
            std::map<std::string, std::vector<std::byte>> _tiles;
        };
    } // namespace

    std::uint32_t Navmesh::LinkForPoly(std::uint64_t ref, const dtMeshTile &tile, const dtPoly &poly) const {
        if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
            // The codec verifies the polygon/connection pairing.
            return tile.offMeshCons[static_cast<int>(&poly - tile.polys) - tile.header->offMeshBase].userId;
        }
        if ((poly.flags & TileCodec::kDoorwayFlag) == 0) {
            return 0;
        }
        const auto doorway = _doorways.find(ref);
        return doorway != _doorways.end() ? doorway->second : 0;
    }

    void Navmesh::CollectPathDoors(std::span<const std::uint64_t> corridor, Path &path) const {
        for (const auto ref : corridor) {
            const dtMeshTile *tile = nullptr;
            const dtPoly *poly     = nullptr;
            if (dtStatusFailed(_mesh->getTileAndPolyByRef(ref, &tile, &poly))) {
                continue;
            }
            const auto add = [&](std::uint32_t id) {
                if (id == 0 || id > _links.size() || _links[id - 1].kind != TileCodec::kDoorLinkKind) {
                    return;
                }
                const auto guid = _links[id - 1].entityGuid;
                if (std::any_of(path.doors.begin(), path.doors.end(), [guid](const PathDoor &door) { return door.guid == guid; })) {
                    return;
                }
                const auto center = _doorCenters.find(guid);
                glm::vec3 position {0.0F};
                if (center != _doorCenters.end()) {
                    position = center->second;
                }
                else {
                    for (unsigned int i = 0; i < poly->vertCount; ++i) {
                        position += ToGame(&tile->verts[poly->verts[i] * 3]);
                    }
                    position /= static_cast<float>(poly->vertCount);
                }
                path.doors.push_back({guid, position});
            };
            const auto id = LinkForPoly(ref, *tile, *poly);
            if (id == 0) {
                continue;
            }
            add(id);
            if (const auto shared = _sharedDoorways.find(id); shared != _sharedDoorways.end()) {
                for (const auto other : shared->second) {
                    add(other);
                }
            }
        }
    }

    Navmesh::Navmesh(): _mesh(nullptr, dtFreeNavMesh), _query(nullptr, dtFreeNavMeshQuery) {}

    Navmesh::~Navmesh() = default;

    void Navmesh::Clear() {
        _query.reset();
        _mesh.reset();
        _base = Source {};
        _overlays.clear();
        _reader.reset();
        _active.clear();
        _links.clear();
        _linkIds.clear();
        _doorways.clear();
        _leafLinks.clear();
        _syntheticDoorways.clear();
        _leafFlags.clear();
        _sharedDoorways.clear();
        _syntheticDoorCount = 0;
    }

    void Navmesh::SetDoorLeaves(std::vector<DoorLeaf> leaves) {
        _leaves = std::move(leaves);
        _doorCenters.clear();
        for (const auto &leaf : _leaves) {
            _doorCenters.emplace(leaf.entityGuid, (leaf.from + leaf.to) * 0.5F);
        }
    }

    void Navmesh::RegisterDoorLeaves() {
        _leafLinks.assign(_leaves.size(), 0);
        for (std::size_t i = 0; i < _leaves.size(); ++i) {
            if (!_linkIds.contains(_leaves[i].entityGuid)) {
                _leafLinks[i] = LinkId(EntityLink {_leaves[i].entityGuid, TileCodec::kDoorLinkKind});
            }
        }
    }

    bool Navmesh::Index(std::span<const std::byte> index, std::uint64_t tilesSize, Source &out, std::string &error) {
        const std::optional<std::vector<TileRecord>> records = TileCodec::ParseIndex(index, static_cast<std::size_t>(tilesSize));
        if (!records) {
            error = "the tile index is malformed";
            return false;
        }
        for (const TileRecord &record : *records) {
            out.columns[Column {record.x, record.y}].push_back(record);
        }
        out.tilesSize = tilesSize;
        return true;
    }

    bool Navmesh::Validate(const Source &source, std::span<const std::byte> tiles, std::string &error) {
        const fu2::function<std::uint32_t(const EntityLink &) const> noIds = [](const EntityLink &) { return 0U; };
        for (const auto &[column, records] : source.columns) {
            for (const TileRecord &record : records) {
                DecodedTile decoded;
                if (!TileCodec::Decode(TileBytes(tiles, record), noIds, decoded, error)) {
                    error = "tile " + std::to_string(record.x) + "," + std::to_string(record.y) + ": " + error;
                    return false;
                }
                dtFree(decoded.data);
            }
        }
        return true;
    }

    bool Navmesh::ReadOverlay(const std::string &name, std::vector<std::byte> &out, std::string &error) const {
        if (!_reader->ReadTiles(name, out, error)) {
            return false;
        }
        const std::uint64_t expected = _overlays.at(name).tilesSize;
        if (out.size() != expected) {
            error = "its tiles are " + std::to_string(out.size()) + " bytes, its index was read for " + std::to_string(expected);
            return false;
        }
        return true;
    }

    std::uint32_t Navmesh::LinkId(const EntityLink &link) {
        const auto [found, inserted] = _linkIds.try_emplace(link.entityGuid, static_cast<std::uint32_t>(_links.size() + 1));
        if (inserted) {
            _links.push_back(link);
        }
        return found->second;
    }

    bool Navmesh::AddTiles(std::span<const std::byte> tiles, const std::vector<TileRecord> &records, std::vector<std::uint64_t> &added, std::string &error) {
        const fu2::function<std::uint32_t(const EntityLink &) const> assignId = [this](const EntityLink &link) { return LinkId(link); };
        for (const TileRecord &record : records) {
            DecodedTile decoded;
            if (!TileCodec::Decode(TileBytes(tiles, record), assignId, decoded, error)) {
                error = "tile " + std::to_string(record.x) + "," + std::to_string(record.y) + ": " + error;
                return false;
            }
            dtTileRef tileRef     = 0;
            const dtStatus status = _mesh->addTile(decoded.data, decoded.size, DT_TILE_FREE_DATA, 0, &tileRef);
            if (dtStatusFailed(status)) {
                dtFree(decoded.data);
                error = "Detour refused tile " + std::to_string(record.x) + "," + std::to_string(record.y) + " (status " + std::to_string(status) + ")";
                return false;
            }
            added.push_back(tileRef);
        }
        return true;
    }

    void Navmesh::IndexDoorways(const dtMeshTile &tile) {
        const dtNavMesh &mesh      = *_mesh;
        const dtPolyRef base       = mesh.getPolyRefBase(&tile);
        const dtMeshHeader &header = *tile.header;
        std::vector<const dtMeshTile *> around(64);
        for (int i = 0; i < header.offMeshBase; ++i) {
            const dtPoly &poly = tile.polys[i];
            if ((poly.flags & TileCodec::kDoorwayFlag) == 0 || _leafFlags.contains(base | static_cast<dtPolyRef>(i))) {
                continue;
            }
            float center[3] = {};
            for (int j = 0; j < poly.vertCount; ++j) {
                dtVadd(center, center, &tile.verts[poly.verts[j] * 3]);
            }
            dtVscale(center, center, 1.0F / static_cast<float>(poly.vertCount));

            // A door link is a metre-long hop through the doorway, stored in the tile its start
            // lies in -- which on a border is not always the tile holding the doorway.
            std::uint32_t nearestId = 0;
            float nearest           = kDoorwayReach * kDoorwayReach;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int count = mesh.getTilesAt(header.x + dx, header.y + dy, around.data(), static_cast<int>(around.size()));
                    for (int t = 0; t < count; ++t) {
                        const dtMeshTile &other = *around[static_cast<std::size_t>(t)];
                        for (int c = 0; c < other.header->offMeshConCount; ++c) {
                            const dtOffMeshConnection &connection = other.offMeshCons[c];
                            if (connection.userId == 0 || _links[connection.userId - 1].kind != TileCodec::kDoorLinkKind) {
                                continue;
                            }
                            float middle[3] = {};
                            dtVlerp(middle, &connection.pos[0], &connection.pos[3], 0.5F);
                            const float distance = dtVdist2DSqr(middle, center);
                            if (distance < nearest && std::fabs(middle[1] - center[1]) < kDoorwayReach) {
                                nearest   = distance;
                                nearestId = connection.userId;
                            }
                        }
                    }
                }
            }
            if (nearestId != 0) {
                _doorways[base | static_cast<dtPolyRef>(i)] = nearestId;
            }
        }
        MarkDoorLeaves(tile);
    }

    namespace {
        /** Whether a point lies inside a convex polygon, on the ground plane (Detour's x and z). */
        bool InsideConvex(const float *point, const std::vector<const float *> &verts) {
            float sign = 0.0F;
            for (std::size_t j = 0; j < verts.size(); ++j) {
                const float *a    = verts[j];
                const float *b    = verts[(j + 1) % verts.size()];
                const float cross = (b[0] - a[0]) * (point[2] - a[2]) - (b[2] - a[2]) * (point[0] - a[0]);
                if (cross != 0.0F) {
                    if (sign != 0.0F && (cross > 0.0F) != (sign > 0.0F)) {
                        return false;
                    }
                    sign = cross;
                }
            }
            return true;
        }

        /** Whether two segments cross on the ground plane. */
        bool SegmentsCross(const float *p, const float *q, const float *a, const float *b) {
            const auto side = [](const float *o, const float *u, const float *v) {
                return (u[0] - o[0]) * (v[2] - o[2]) - (u[2] - o[2]) * (v[0] - o[0]);
            };
            const float d1 = side(a, b, p);
            const float d2 = side(a, b, q);
            const float d3 = side(p, q, a);
            const float d4 = side(p, q, b);
            return ((d1 > 0.0F) != (d2 > 0.0F)) && ((d3 > 0.0F) != (d4 > 0.0F));
        }
    } // namespace

    void Navmesh::MarkDoorLeaves(const dtMeshTile &tile) {
        if (_leafLinks.empty()) {
            return;
        }
        const dtMeshHeader &header = *tile.header;
        const dtPolyRef base       = static_cast<const dtNavMesh &>(*_mesh).getPolyRefBase(&tile);
        std::vector<const float *> verts;
        for (std::size_t l = 0; l < _leaves.size(); ++l) {
            const std::uint32_t linkId = _leafLinks[l];
            if (linkId == 0) {
                continue;
            }
            float from[3] = {};
            float to[3]   = {};
            ToDetour(_leaves[l].from, from);
            ToDetour(_leaves[l].to, to);
            if (std::max(from[0], to[0]) < header.bmin[0] || std::min(from[0], to[0]) > header.bmax[0] || std::max(from[2], to[2]) < header.bmin[2] || std::min(from[2], to[2]) > header.bmax[2] || from[1] < header.bmin[1] - kLeafHeightReach || from[1] > header.bmax[1] + kLeafHeightReach) {
                continue;
            }
            for (int i = 0; i < header.offMeshBase; ++i) {
                const dtPoly &poly = tile.polys[i];
                verts.clear();
                float height = 0.0F;
                for (int j = 0; j < poly.vertCount; ++j) {
                    verts.push_back(&tile.verts[poly.verts[j] * 3]);
                    height += verts.back()[1];
                }
                height /= static_cast<float>(poly.vertCount);
                if (std::fabs(height - from[1]) > kLeafHeightReach) {
                    continue;
                }
                bool crossed = InsideConvex(from, verts) || InsideConvex(to, verts);
                for (std::size_t j = 0; j < verts.size() && !crossed; ++j) {
                    crossed = SegmentsCross(from, to, verts[j], verts[(j + 1) % verts.size()]);
                }
                if (!crossed) {
                    continue;
                }
                const dtPolyRef ref = base | static_cast<dtPolyRef>(i);
                const auto marked   = _doorways.find(ref);
                if (marked == _doorways.end()) {
                    if ((poly.flags & TileCodec::kDoorwayFlag) == 0) {
                        _mesh->setPolyFlags(ref, static_cast<unsigned short>(poly.flags | TileCodec::kDoorwayFlag));
                        _leafFlags.insert(ref);
                    }
                    _doorways[ref] = linkId;
                    _syntheticDoorways.insert(ref);
                }
                else if (_syntheticDoorways.contains(ref) && marked->second != linkId) {
                    // Another catalogued door stands in the same doorway: the level places
                    // variants of one door at one spot, and the doorway is only clear when
                    // both may be crossed.
                    for (const auto [owner, other] : {std::pair {marked->second, linkId}, std::pair {linkId, marked->second}}) {
                        std::vector<std::uint32_t> &others = _sharedDoorways[owner];
                        if (std::find(others.begin(), others.end(), other) == others.end()) {
                            others.push_back(other);
                        }
                    }
                }
            }
        }
    }

    void Navmesh::ForgetDoorways(const dtMeshTile &tile) {
        const dtPolyRef base = static_cast<const dtNavMesh &>(*_mesh).getPolyRefBase(&tile);
        for (int i = 0; i < tile.header->offMeshBase; ++i) {
            const dtPolyRef ref = base | static_cast<dtPolyRef>(i);
            _syntheticDoorways.erase(ref);
            if (_leafFlags.erase(ref) != 0) {
                _mesh->setPolyFlags(ref, static_cast<unsigned short>(tile.polys[i].flags & ~TileCodec::kDoorwayFlag));
            }
            _doorways.erase(ref);
        }
    }

    bool Navmesh::Build(MeshFiles base, std::map<std::string, MeshFiles> overlays, std::vector<std::string> &rejected, std::string &error) {
        std::map<std::string, OverlayIndex> indexes;
        std::map<std::string, std::vector<std::byte>> tiles;
        for (auto &[name, files] : overlays) {
            indexes.emplace(name, OverlayIndex {std::move(files.index), files.tiles.size()});
            tiles.emplace(name, std::move(files.tiles));
        }
        return Build(std::move(base), std::move(indexes), std::make_unique<MemoryOverlayReader>(std::move(tiles)), rejected, error);
    }

    bool Navmesh::Build(MeshFiles base, std::map<std::string, OverlayIndex> overlays, std::unique_ptr<OverlayReader> reader, std::vector<std::string> &rejected, std::string &error) {
        Clear();

        Source baseSource;
        if (!Index(base.index, base.tiles.size(), baseSource, error)) {
            Clear();
            return false;
        }
        if (baseSource.columns.empty()) {
            error = "the base mesh has no tiles";
            Clear();
            return false;
        }

        std::size_t tileBudget = 0;
        for (const auto &[column, records] : baseSource.columns) {
            tileBudget += records.size();
        }
        for (auto &[name, overlay] : overlays) {
            Source source;
            std::string overlayError;
            if (!Index(overlay.index, overlay.tilesSize, source, overlayError)) {
                rejected.push_back(name + ": " + overlayError);
                continue;
            }
            for (const auto &[column, records] : source.columns) {
                tileBudget += records.size();
            }
            _overlays.emplace(name, std::move(source));
        }

        // Every tile is a 16 m square on a grid anchored at the origin; the first tile says how big.
        const TileRecord &first = baseSource.columns.begin()->second.front();
        DecodedTile probe;
        const fu2::function<std::uint32_t(const EntityLink &) const> noIds = [](const EntityLink &) { return 0U; };
        if (!TileCodec::Decode(TileBytes(base.tiles, first), noIds, probe, error)) {
            error = "tile " + std::to_string(first.x) + "," + std::to_string(first.y) + ": " + error;
            Clear();
            return false;
        }
        const auto *probeHeader = reinterpret_cast<const dtMeshHeader *>(probe.data);
        dtNavMeshParams params {};
        params.tileWidth  = probeHeader->bmax[0] - probeHeader->bmin[0];
        params.tileHeight = probeHeader->bmax[2] - probeHeader->bmin[2];
        params.orig[0]    = probeHeader->bmin[0] - static_cast<float>(probeHeader->x) * params.tileWidth;
        params.orig[1]    = 0.0F;
        params.orig[2]    = probeHeader->bmin[2] - static_cast<float>(probeHeader->y) * params.tileHeight;
        dtFree(probe.data);
        // Room for the base mesh and every overlay at once, which is more than can ever be loaded.
        params.maxTiles = static_cast<int>(dtNextPow2(static_cast<unsigned int>(tileBudget)));
        params.maxPolys = 1 << DT_POLY_BITS;

        _mesh.reset(dtAllocNavMesh());
        if (!_mesh || dtStatusFailed(_mesh->init(&params))) {
            error = "Detour could not size a mesh for " + std::to_string(tileBudget) + " tiles";
            Clear();
            return false;
        }

        std::vector<std::uint64_t> added;
        for (const auto &[column, records] : baseSource.columns) {
            if (!AddTiles(base.tiles, records, added, error)) {
                Clear();
                return false;
            }
        }
        // Only once every tile is in: a doorway's link can sit in the tile next to it,
        // and a door no tile names a link for is only known not to have one now.
        RegisterDoorLeaves();
        const dtNavMesh &mesh = *_mesh;
        for (const std::uint64_t tileRef : added) {
            IndexDoorways(*mesh.getTileByRef(tileRef));
        }
        std::unordered_set<std::uint32_t> gated;
        for (const auto &[ref, linkId] : _doorways) {
            if (_syntheticDoorways.contains(ref)) {
                gated.insert(linkId);
            }
        }
        for (const auto &[linkId, others] : _sharedDoorways) {
            gated.insert(linkId);
        }
        _syntheticDoorCount = gated.size();

        _query.reset(dtAllocNavMeshQuery());
        if (!_query || dtStatusFailed(_query->init(_mesh.get(), kMaxSearchNodes))) {
            error = "Detour could not allocate a query";
            Clear();
            return false;
        }

        // Of the base mesh, keep only the columns an overlay can take away and later give back.
        for (const auto &[name, overlay] : _overlays) {
            for (const auto &[column, records] : overlay.columns) {
                const auto covered = baseSource.columns.find(column);
                if (covered == baseSource.columns.end() || _base.columns.contains(column)) {
                    continue;
                }
                std::vector<TileRecord> &kept = _base.columns[column];
                for (const TileRecord &record : covered->second) {
                    const std::span<const std::byte> bytes = TileBytes(base.tiles, record);
                    kept.push_back(TileRecord {record.x, record.y, static_cast<std::uint32_t>(_base.tiles.size()), record.size});
                    _base.tiles.insert(_base.tiles.end(), bytes.begin(), bytes.end());
                }
            }
        }
        _base.tilesSize = _base.tiles.size();
        _base.tiles.shrink_to_fit();
        _reader = std::move(reader);

        _corridor.resize(kMaxPathPolygons);
        _visited.resize(kMaxPathPolygons);
        _corners.resize(static_cast<std::size_t>(kMaxPathPolygons) * 3);
        return true;
    }

    std::size_t Navmesh::TileCount() const {
        if (!_mesh) {
            return 0;
        }
        const dtNavMesh &mesh = *_mesh;
        std::size_t count     = 0;
        for (int i = 0; i < mesh.getMaxTiles(); ++i) {
            count += mesh.getTile(i)->header != nullptr ? 1 : 0;
        }
        return count;
    }

    std::size_t Navmesh::PolygonCount() const {
        if (!_mesh) {
            return 0;
        }
        const dtNavMesh &mesh = *_mesh;
        std::size_t count     = 0;
        for (int i = 0; i < mesh.getMaxTiles(); ++i) {
            const dtMeshTile *tile = mesh.getTile(i);
            count += tile->header != nullptr ? static_cast<std::size_t>(tile->header->polyCount) : 0;
        }
        return count;
    }

    std::vector<std::string> Navmesh::OverlayNames() const {
        std::vector<std::string> names;
        names.reserve(_overlays.size());
        for (const auto &[name, source] : _overlays) {
            names.push_back(name);
        }
        return names;
    }

    bool Navmesh::RebuildColumns(const std::vector<Column> &columns, std::map<std::string, std::vector<std::byte>> &loaded, std::string &error) {
        const dtNavMesh &mesh = *_mesh;
        std::vector<const dtMeshTile *> tiles(64);
        bool rebuilt = true;
        for (const Column &column : columns) {
            const int count = mesh.getTilesAt(column.x, column.y, tiles.data(), static_cast<int>(tiles.size()));
            for (int i = 0; i < count; ++i) {
                const dtMeshTile *tile = tiles[static_cast<std::size_t>(i)];
                ForgetDoorways(*tile);
                _mesh->removeTile(mesh.getTileRef(tile), nullptr, nullptr);
            }

            const Source *source = &_base;
            std::string sourceName;
            for (auto name = _active.rbegin(); name != _active.rend(); ++name) {
                const Source &overlay = _overlays.at(*name);
                if (overlay.columns.contains(column)) {
                    source     = &overlay;
                    sourceName = *name;
                    break;
                }
            }
            const auto records = source->columns.find(column);
            if (records == source->columns.end()) {
                continue;
            }

            std::span<const std::byte> bytes = _base.tiles;
            std::string columnError;
            if (!sourceName.empty()) {
                auto cached = loaded.find(sourceName);
                if (cached == loaded.end()) {
                    std::vector<std::byte> read;
                    if (!ReadOverlay(sourceName, read, columnError)) {
                        if (rebuilt) {
                            error = "overlay " + sourceName + ": " + columnError;
                        }
                        rebuilt = false;
                        continue;
                    }
                    cached = loaded.emplace(sourceName, std::move(read)).first;
                }
                bytes = cached->second;
            }
            std::vector<std::uint64_t> added;
            if (!AddTiles(bytes, records->second, added, columnError)) {
                if (rebuilt) {
                    error = columnError;
                }
                rebuilt = false;
            }
        }

        // The columns' own doorways are new, and a neighbour's may have been tied to a link that just went.
        std::unordered_set<Column, ColumnHash> around;
        for (const Column &column : columns) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    around.insert(Column {column.x + dx, column.y + dy});
                }
            }
        }
        for (const Column &column : around) {
            const int count = mesh.getTilesAt(column.x, column.y, tiles.data(), static_cast<int>(tiles.size()));
            for (int i = 0; i < count; ++i) {
                ForgetDoorways(*tiles[static_cast<std::size_t>(i)]);
                IndexDoorways(*tiles[static_cast<std::size_t>(i)]);
            }
        }
        return rebuilt;
    }

    bool Navmesh::EnableOverlay(const std::string &name, std::string &error) {
        error.clear();
        const auto overlay = _overlays.find(name);
        if (!_mesh || overlay == _overlays.end() || std::find(_active.begin(), _active.end(), name) != _active.end()) {
            return false;
        }

        // Read and check every tile before the mesh changes, so a broken overlay leaves it as it was.
        std::map<std::string, std::vector<std::byte>> loaded;
        if (!overlay->second.columns.empty()) {
            std::vector<std::byte> &tiles = loaded[name];
            if (!ReadOverlay(name, tiles, error) || !Validate(overlay->second, tiles, error)) {
                error = "overlay " + name + ": " + error;
                return false;
            }
        }

        std::vector<Column> columns;
        columns.reserve(overlay->second.columns.size());
        for (const auto &[column, records] : overlay->second.columns) {
            columns.push_back(column);
        }
        _active.push_back(name);
        if (!RebuildColumns(columns, loaded, error)) {
            // Only Detour itself can refuse a checked tile; give the columns back to what lay underneath.
            _active.pop_back();
            std::string ignored;
            RebuildColumns(columns, loaded, ignored);
            return false;
        }
        return true;
    }

    bool Navmesh::DisableOverlay(const std::string &name, std::string &error) {
        error.clear();
        const auto active = std::find(_active.begin(), _active.end(), name);
        if (active == _active.end()) {
            return false;
        }
        _active.erase(active);

        std::vector<Column> columns;
        for (const auto &[column, records] : _overlays.at(name).columns) {
            columns.push_back(column);
        }
        std::map<std::string, std::vector<std::byte>> loaded;
        return RebuildColumns(columns, loaded, error);
    }

    float Navmesh::RandomUnit() {
        static std::mt19937 generator(0x6B636463U);
        return std::uniform_real_distribution<float>(0.0F, 1.0F)(generator);
    }

    std::uint64_t Navmesh::Nearest(const glm::vec3 &point, const QueryOptions &options, const Filter &filter, glm::vec3 &onMesh) const {
        float center[3] = {};
        ToDetour(point, center);
        const float extents[3] = {options.searchRadius, options.searchHeight, options.searchRadius};
        dtPolyRef ref          = 0;
        float nearest[3]       = {};
        if (dtStatusFailed(_query->findNearestPoly(center, extents, &filter, &ref, nearest)) || ref == 0) {
            return 0;
        }
        onMesh = ToGame(nearest);
        return ref;
    }

    std::optional<glm::vec3> Navmesh::ClosestPoint(const glm::vec3 &point, const QueryOptions &options) const {
        if (!IsReady()) {
            return std::nullopt;
        }
        const Filter filter(*this, options.linkGate);
        glm::vec3 onMesh {0.0F};
        if (Nearest(point, options, filter, onMesh) == 0) {
            return std::nullopt;
        }
        return onMesh;
    }

    std::optional<float> Navmesh::FloorAt(const glm::vec3 &point, const QueryOptions &options) const {
        const std::optional<glm::vec3> onMesh = ClosestPoint(point, options);
        if (!onMesh) {
            return std::nullopt;
        }
        return onMesh->z;
    }

    std::optional<Navmesh::Path> Navmesh::FindPath(const glm::vec3 &from, const glm::vec3 &to, const QueryOptions &options) const {
        if (!IsReady()) {
            return std::nullopt;
        }
        const Filter filter(*this, options.linkGate);
        glm::vec3 start {0.0F};
        glm::vec3 goal {0.0F};
        const dtPolyRef startRef = Nearest(from, options, filter, start);
        const dtPolyRef goalRef  = Nearest(to, options, filter, goal);
        if (startRef == 0 || goalRef == 0) {
            return std::nullopt;
        }

        float startPos[3] = {};
        float goalPos[3]  = {};
        ToDetour(start, startPos);
        ToDetour(goal, goalPos);

        // The sliced search is the plain one with an iteration cap, which is what makes the budget exact.
        int corridorCount = 0;
        dtStatus status   = _query->initSlicedFindPath(startRef, goalRef, startPos, goalPos, &filter);
        if (dtStatusInProgress(status)) {
            status = _query->updateSlicedFindPath(std::clamp(options.maxSearchNodes, 1, kMaxSearchNodes), nullptr);
        }
        status = _query->finalizeSlicedFindPath(_corridor.data(), &corridorCount, kMaxPathPolygons);
        if (dtStatusFailed(status) || corridorCount == 0) {
            return std::nullopt;
        }

        Path path;
        path.complete = !dtStatusDetail(status, DT_PARTIAL_RESULT) && _corridor[static_cast<std::size_t>(corridorCount - 1)] == goalRef;

        // A* picks its corridor by polygon centres, so across tile borders the funnel bends around
        // portals a straight walk would not touch. When the surface itself runs straight to the goal
        // polygon, that line is the path.
        if (path.complete) {
            float t            = 0.0F;
            float normal[3]    = {};
            int visitedCount   = 0;
            const dtStatus ray = _query->raycast(startRef, startPos, goalPos, &filter, &t, normal, _visited.data(), &visitedCount, kMaxPathPolygons);
            if (dtStatusSucceed(ray) && !dtStatusDetail(ray, DT_BUFFER_TOO_SMALL) && t > 1.0F && visitedCount > 0 && _visited[static_cast<std::size_t>(visitedCount - 1)] == goalRef) {
                path.points = {start, goal};
                CollectPathDoors(std::span(_visited.data(), static_cast<std::size_t>(visitedCount)), path);
                return path;
            }
        }
        if (!path.complete) {
            // Detour's own advice: aim the corners at the nearest point of the polygon the search reached.
            float reached[3] = {};
            if (dtStatusFailed(_query->closestPointOnPoly(_corridor[static_cast<std::size_t>(corridorCount - 1)], goalPos, reached, nullptr))) {
                return std::nullopt;
            }
            dtVcopy(goalPos, reached);
        }

        int cornerCount = 0;
        if (dtStatusFailed(_query->findStraightPath(startPos, goalPos, _corridor.data(), corridorCount, _corners.data(), nullptr, nullptr, &cornerCount, kMaxPathPolygons, 0))) {
            return std::nullopt;
        }
        CollectPathDoors(std::span(_corridor.data(), static_cast<std::size_t>(corridorCount)), path);
        path.points.reserve(static_cast<std::size_t>(cornerCount));
        for (int i = 0; i < cornerCount; ++i) {
            path.points.push_back(ToGame(&_corners[static_cast<std::size_t>(i) * 3]));
        }
        return path;
    }

    std::optional<Navmesh::RayHit> Navmesh::Raycast(const glm::vec3 &from, const glm::vec3 &to, const QueryOptions &options) const {
        if (!IsReady()) {
            return std::nullopt;
        }
        const Filter filter(*this, options.linkGate);
        glm::vec3 start {0.0F};
        const dtPolyRef startRef = Nearest(from, options, filter, start);
        if (startRef == 0) {
            return std::nullopt;
        }

        float startPos[3] = {};
        float endPos[3]   = {};
        ToDetour(start, startPos);
        ToDetour(to, endPos);

        float t          = 0.0F;
        float normal[3]  = {};
        int visitedCount = 0;
        if (dtStatusFailed(_query->raycast(startRef, startPos, endPos, &filter, &t, normal, _visited.data(), &visitedCount, kMaxPathPolygons)) || visitedCount == 0) {
            return std::nullopt;
        }

        RayHit result;
        result.hit    = t <= 1.0F;
        float stop[3] = {};
        dtVlerp(stop, startPos, endPos, result.hit ? t : 1.0F);
        // The ray runs in the plane; put where it stopped back on the surface it crossed.
        float onSurface[3] = {};
        if (dtStatusSucceed(_query->closestPointOnPoly(_visited[static_cast<std::size_t>(visitedCount - 1)], stop, onSurface, nullptr))) {
            dtVcopy(stop, onSurface);
        }
        result.point = ToGame(stop);
        return result;
    }

    std::optional<glm::vec3> Navmesh::RandomPoint(const QueryOptions &options) const {
        if (!IsReady()) {
            return std::nullopt;
        }
        const Filter filter(*this, options.linkGate);
        dtPolyRef ref  = 0;
        float point[3] = {};
        if (dtStatusFailed(_query->findRandomPoint(&filter, &Navmesh::RandomUnit, &ref, point)) || ref == 0) {
            return std::nullopt;
        }
        return ToGame(point);
    }

    std::optional<glm::vec3> Navmesh::RandomPoint(const glm::vec3 &center, float radius, const QueryOptions &options) const {
        if (!IsReady() || !(radius > 0.0F)) {
            return std::nullopt;
        }
        const Filter filter(*this, options.linkGate);
        glm::vec3 start {0.0F};
        const dtPolyRef startRef = Nearest(center, options, filter, start);
        if (startRef == 0) {
            return std::nullopt;
        }
        float startPos[3] = {};
        ToDetour(start, startPos);
        for (int attempt = 0; attempt < kRandomPointAttempts; ++attempt) {
            dtPolyRef ref  = 0;
            float point[3] = {};
            if (dtStatusFailed(_query->findRandomPointAroundCircle(startRef, startPos, radius, &filter, &Navmesh::RandomUnit, &ref, point)) || ref == 0) {
                return std::nullopt;
            }
            if (dtVdist2DSqr(point, startPos) <= radius * radius) {
                return ToGame(point);
            }
        }
        return std::nullopt;
    }
} // namespace KCDC::Features::Navigation
