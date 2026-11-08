#include "features/navigation/navigation_service.h"
#include "features/navigation/navmesh.h"
#include "features/navigation/navmesh_codec.h"
#include "features/navigation/navmesh_discovery.h"
#include "features/npc/npc_navigation.h"
#include "features/patrol/patrol_validation.h"

#include "shared/features/door/door_entity.h"

#include <DetourCommon.h>
#include <DetourNavMesh.h>

#include <miniz.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <function2/function2.hpp>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    void Require(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    bool Near(float a, float b, float tolerance = 0.05F) {
        return std::fabs(a - b) <= tolerance;
    }

    class PakFixture final {
      public:
        PakFixture(): _root(std::filesystem::current_path() / ("navmesh-discovery-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
            Require(std::filesystem::create_directory(_root), "the discovery fixture owns a new directory");
        }

        ~PakFixture() {
            std::error_code error;
            std::filesystem::remove_all(_root, error);
        }

        std::filesystem::path Path(const std::filesystem::path &relative) const {
            return _root / relative;
        }

        std::filesystem::path Pak(const std::filesystem::path &relative) const {
            const std::filesystem::path path = Path(relative);
            std::filesystem::create_directories(path.parent_path());
            std::ofstream file(path);
            Require(file.good(), "the discovery fixture can create a pak candidate");
            return std::filesystem::canonical(path);
        }

      private:
        std::filesystem::path _root;
    };

    void DiscoveryTests() {
        const PakFixture fixture;
        fixture.Pak("server/recast.pak");
        const auto game   = fixture.Pak("game/Data/Levels/kutnohorsko/recast.pak");
        const auto server = fixture.Path("server");
        Require(KCDC::Features::Navigation::FindNavmeshPaks("", "kutnohorsko", server).empty(), "automatic discovery ignores server-root paks and neighbouring game installs");

        const auto nested = fixture.Pak("server/files/meshes/kutnohorsko/recast.pak");
        Require(KCDC::Features::Navigation::FindNavmeshPaks("", "kutnohorsko", server) == std::vector<std::filesystem::path> {nested}, "an omitted setting discovers a nested files copy");
        for (const auto &source : {game, fixture.Path("game"), fixture.Path("game/Data"), fixture.Path("game/Data/Levels"), game.parent_path()}) {
            const auto paks = KCDC::Features::Navigation::FindNavmeshPaks(source.string(), "kutnohorsko", server);
            Require(paks == std::vector<std::filesystem::path> {game, nested}, "explicit file and directory layouts take priority over bundled copies");
        }
        Require(KCDC::Features::Navigation::FindNavmeshPaks("../game", "kutnohorsko", server).front() == game, "configured relative paths start at the server working directory");

        const auto bare  = fixture.Pak("server/files/recast.pak");
        const auto first = fixture.Pak("server/files/a/recast.pak");
        const auto last  = fixture.Pak("server/files/z/recast.pak");
        // Only a directory spelled as a level is that level's, in any case; one merely starting with a level's name is neutral.
        const auto upper = fixture.Pak("server/files/Upper/KUTNOHORSKO/Recast.PAK");
        const auto old   = fixture.Pak("server/files/kutnohorsko_old/recast.pak");
        fixture.Pak("server/files/trosecko/recast.pak");
        fixture.Pak("server/files/klaster/recast.pak");
        fixture.Pak("server/files/TROSECKO/nested/RECAST.pak");
        const std::vector<std::filesystem::path> expected {upper, nested, first, old, last, bare};
        Require(KCDC::Features::Navigation::FindNavmeshPaks("", "kutnohorsko", server) == expected, "copies under the level's name come first, then unnamed ones, the bare copy last, and other levels are skipped");
        Require(KCDC::Features::Navigation::FindNavmeshPaks("", "Kutnohorsko", server) == expected, "the level name matches in any case");
        Require(KCDC::Features::Navigation::FindNavmeshPaks("missing", "kutnohorsko", server) == expected, "a missing configured path falls back to the files tree");
        const std::vector<std::filesystem::path> configuredBare {bare, upper, nested, first, old, last};
        Require(KCDC::Features::Navigation::FindNavmeshPaks(bare.string(), "kutnohorsko", server) == configuredBare, "a configured copy is not tried again during discovery");
        const auto renamed = fixture.Pak("game/named.pak");
        Require(KCDC::Features::Navigation::FindNavmeshPaks(renamed.string(), "kutnohorsko", server).front() == renamed, "an explicitly configured file may have a different name");
    }

    /** Writes a tile the way the game serializes one, so the tests exercise the codec rather than restate it. */
    class GameTileWriter final {
      public:
        struct Poly {
            std::vector<std::uint16_t> verts;
            std::vector<std::uint16_t> neis;
            std::uint8_t flags = 1;
            bool offMesh       = false;
        };

        struct Link {
            glm::vec3 from {0.0F};
            glm::vec3 to {0.0F};
            std::uint64_t guid = 0;
            std::int32_t kind  = 0;
        };

        std::vector<glm::vec3> packedVerts;
        std::vector<glm::vec3> floatVerts;
        std::vector<Poly> polys;
        std::vector<Link> links;
        std::int32_t version = KCDC::Features::Navigation::TileCodec::kGameTileVersion;

        static constexpr float kMinZ = -1.0F;
        static constexpr float kMaxZ = 1.0F;

        std::vector<std::byte> Write() const {
            std::vector<std::byte> out;
            const auto vertCount        = static_cast<std::int32_t>(packedVerts.size() + floatVerts.size());
            const auto polyCount        = static_cast<std::int32_t>(polys.size());
            const auto linkCount        = static_cast<std::int32_t>(links.size());
            const std::int32_t ints[17] = {DT_NAVMESH_MAGIC, version, 0, 0, 0, 0, polyCount, vertCount, static_cast<std::int32_t>(packedVerts.size()), 0, 0, 0, 0, 0, 0, linkCount, polyCount - linkCount};
            for (const std::int32_t value : ints) {
                Put(out, value);
            }
            const float floats[11] = {1.76F, 0.32F, 0.32F, 0.8F, 0.0F, 0.0F, kMinZ, 16.0F, 16.0F, kMaxZ, 12.5F};
            for (const float value : floats) {
                Put(out, value);
            }
            for (const glm::vec3 &vert : packedVerts) {
                const auto x = static_cast<std::uint32_t>(std::lround(vert.x / 16.0F * 2047.0F));
                const auto y = static_cast<std::uint32_t>(std::lround(vert.y / 16.0F * 2047.0F));
                const auto z = static_cast<std::uint32_t>(std::lround((vert.z - kMinZ) / (kMaxZ - kMinZ) * 1023.0F));
                Put(out, (x << 21) | (y << 10) | z);
            }
            for (const glm::vec3 &vert : floatVerts) {
                Put(out, vert.x);
                Put(out, vert.y);
                Put(out, vert.z);
            }
            for (const Poly &poly : polys) {
                for (std::size_t j = 0; j < 6; ++j) {
                    Put(out, j < poly.verts.size() ? poly.verts[j] : std::uint16_t {0});
                }
                for (std::size_t j = 0; j < 6; ++j) {
                    Put(out, j < poly.neis.size() ? poly.neis[j] : std::uint16_t {0});
                }
                Put(out, poly.flags);
                Put(out, static_cast<std::uint8_t>(poly.verts.size()));
                Put(out, static_cast<std::uint8_t>(poly.offMesh ? 1 : 0));
                Put(out, std::uint8_t {1});
            }
            for (std::size_t i = 0; i < links.size(); ++i) {
                const Link &link = links[i];
                for (const float value : {link.from.x, link.from.y, link.from.z, link.to.x, link.to.y, link.to.z, 0.5F}) {
                    Put(out, value);
                }
                Put(out, static_cast<std::uint16_t>(polyCount - linkCount + static_cast<std::int32_t>(i)));
                Put(out, std::uint8_t {1});
                Put(out, std::uint8_t {0xFF});
                Put(out, link.guid);
                Put(out, link.kind);
                for (const std::uint16_t value : {std::uint16_t {0xFFFF}, std::uint16_t {0x7FFF}, std::uint16_t {1}, std::uint16_t {0xFFFF}}) {
                    Put(out, value);
                }
            }
            return out;
        }

      private:
        template <typename T>
        static void Put(std::vector<std::byte> &out, T value) {
            const std::size_t at = out.size();
            out.resize(at + sizeof(T));
            std::memcpy(out.data() + at, &value, sizeof(T));
        }
    };

    std::vector<std::byte> IndexFor(std::uint32_t size) {
        const std::int32_t words[5] = {0, 0, 0, 1, static_cast<std::int32_t>(size)};
        std::vector<std::byte> out(sizeof(words));
        std::memcpy(out.data(), words, sizeof(words));
        return out;
    }

    constexpr std::uint64_t kDoorGuid = 0x0123456789ABCDEFULL;

    /**
     * A corridor across one tile, y 6..10: room A (x 0..7), a doorway (x 7..9)
     * with the door's own off-mesh link through it, and room B (x 9..16).
     * Polygons wind clockwise seen from above, as the game's do.
     */
    GameTileWriter Corridor(bool withDoorway) {
        GameTileWriter tile;
        tile.packedVerts = {{0, 6, 0}, {7, 6, 0}, {9, 6, 0}, {16, 6, 0}, {16, 10, 0}, {9, 10, 0}, {7, 10, 0}, {0, 10, 0}};
        tile.polys.push_back({{0, 7, 6, 1}, {0, 0, static_cast<std::uint16_t>(withDoorway ? 2 : 0), 0}});
        if (withDoorway) {
            tile.polys.push_back({{1, 6, 5, 2}, {1, 0, 3, 0}, static_cast<std::uint8_t>(1 | KCDC::Features::Navigation::TileCodec::kDoorwayFlag)});
        }
        tile.polys.push_back({{2, 5, 4, 3}, {static_cast<std::uint16_t>(withDoorway ? 2 : 0), 0, 0, 0}});
        if (withDoorway) {
            tile.floatVerts = {{7.2F, 8.0F, 0.0F}, {8.8F, 8.0F, 0.0F}};
            tile.polys.push_back({{8, 9}, {0, 0}, 1, true});
            tile.links.push_back({{7.2F, 8.0F, 0.0F}, {8.8F, 8.0F, 0.0F}, kDoorGuid, KCDC::Features::Navigation::TileCodec::kDoorLinkKind});
        }
        return tile;
    }

    /** The corridor with its doorway as some shipped doors have it: plain ground, no flag and no link. */
    GameTileWriter UnmarkedCorridor() {
        GameTileWriter tile;
        tile.packedVerts = {{0, 6, 0}, {7, 6, 0}, {9, 6, 0}, {16, 6, 0}, {16, 10, 0}, {9, 10, 0}, {7, 10, 0}, {0, 10, 0}};
        tile.polys.push_back({{0, 7, 6, 1}, {0, 0, 2, 0}});
        tile.polys.push_back({{1, 6, 5, 2}, {1, 0, 3, 0}});
        tile.polys.push_back({{2, 5, 4, 3}, {2, 0, 0, 0}});
        return tile;
    }

    GameTileWriter Bend() {
        GameTileWriter tile;
        tile.packedVerts = {{0, 0, 0}, {8, 0, 0}, {10, 0, 0}, {10, 2, 0}, {10, 10, 0}, {8, 10, 0}, {8, 2, 0}, {0, 2, 0}};
        tile.polys.push_back({{0, 7, 6, 1}, {0, 0, 2, 0}});
        tile.polys.push_back({{1, 6, 3, 2}, {1, 3, 0, 0}});
        tile.polys.push_back({{6, 5, 4, 3}, {0, 0, 0, 2}});
        return tile;
    }

    KCDC::Features::Navigation::MeshFiles Files(const GameTileWriter &tile) {
        KCDC::Features::Navigation::MeshFiles files;
        files.tiles = tile.Write();
        files.index = IndexFor(static_cast<std::uint32_t>(files.tiles.size()));
        return files;
    }

    template <typename T>
    void Patch(std::vector<std::byte> &bytes, std::size_t at, T value) {
        std::memcpy(bytes.data() + at, &value, sizeof(T));
    }

    // Where the game's layer header keeps the fields the hostile-input tests forge.
    constexpr std::size_t kMaxLinkCountOffset    = 9 * 4;
    constexpr std::size_t kOffMeshConCountOffset = 15 * 4;
    constexpr std::size_t kOffMeshBaseOffset     = 16 * 4;
    constexpr std::size_t kBoundsOffset          = 17 * 4 + 4 * 4;
    constexpr std::size_t kOffMeshSize           = 52;

    void CodecTests() {
        const std::vector<std::byte> bytes = Corridor(true).Write();

        const fu2::function<std::uint32_t(const KCDC::Features::Navigation::EntityLink &) const> ids = [](const KCDC::Features::Navigation::EntityLink &link) {
            Require(link.entityGuid == kDoorGuid && link.kind == KCDC::Features::Navigation::TileCodec::kDoorLinkKind, "the door link carries its door");
            return 7U;
        };

        KCDC::Features::Navigation::DecodedTile decoded;
        std::string error;
        Require(KCDC::Features::Navigation::TileCodec::Decode(bytes, ids, decoded, error), "a well-formed tile decodes");
        const auto *header = reinterpret_cast<const dtMeshHeader *>(decoded.data);
        Require(header->version == DT_NAVMESH_VERSION && header->polyCount == 4 && header->vertCount >= 10 && header->offMeshBase == 3, "the header is rewritten for stock Detour");
        // Padding vertices exist only to put the link section where a 64-bit dtLink can live.
        const int linksOffset = dtAlign4(static_cast<int>(sizeof(dtMeshHeader))) + dtAlign4(12 * header->vertCount) + dtAlign4(static_cast<int>(sizeof(dtPoly)) * header->polyCount);
        Require(linksOffset % static_cast<int>(alignof(dtLink)) == 0, "the link section is aligned for dtLink");
        Require(header->detailMeshCount == 3 && header->detailTriCount == 6, "every ground polygon gets a fan detail mesh");
        // Z-up in, Y-up out: the tile's height range lands in Detour's Y.
        Require(Near(header->bmin[1], GameTileWriter::kMinZ) && Near(header->bmax[1], GameTileWriter::kMaxZ) && Near(header->bmax[2], 16.0F), "bounds are swizzled into Detour's axes");
        dtFree(decoded.data);

        GameTileWriter wrongVersion = Corridor(true);
        wrongVersion.version        = 7;
        Require(!KCDC::Features::Navigation::TileCodec::Decode(wrongVersion.Write(), ids, decoded, error), "a stock Detour tile is not taken for the game's");

        std::vector<std::byte> truncated = bytes;
        truncated.pop_back();
        Require(!KCDC::Features::Navigation::TileCodec::Decode(truncated, ids, decoded, error), "a tile that does not match its counts is refused");

        const std::vector<std::byte> index = IndexFor(static_cast<std::uint32_t>(bytes.size()));
        const auto records                 = KCDC::Features::Navigation::TileCodec::ParseIndex(index, bytes.size());
        Require(records && records->size() == 1 && (*records)[0].size == bytes.size(), "the index flattens to one record per layer");
        Require(!KCDC::Features::Navigation::TileCodec::ParseIndex(index, bytes.size() - 1), "an index pointing past its tiles is refused");
        const std::int32_t tooManyLayers[5] = {0, 0, 0, 300, static_cast<std::int32_t>(bytes.size())};
        std::vector<std::byte> layered(sizeof(tooManyLayers));
        std::memcpy(layered.data(), tooManyLayers, sizeof(tooManyLayers));
        Require(!KCDC::Features::Navigation::TileCodec::ParseIndex(layered, bytes.size()), "a column claiming hundreds of layers is not an index");

        // The game's own link count sizes a different pool; a hostile one changes nothing.
        std::vector<std::byte> hugeLinks = bytes;
        Patch(hugeLinks, kMaxLinkCountOffset, std::numeric_limits<std::int32_t>::max());
        Require(KCDC::Features::Navigation::TileCodec::Decode(hugeLinks, ids, decoded, error), "a tile whose game link count is absurd still decodes");
        Require(reinterpret_cast<const dtMeshHeader *>(decoded.data)->maxLinkCount == 12 + 4 + 16, "the link pool is the stock estimate plus slack, whatever the file says");
        dtFree(decoded.data);

        const std::size_t link          = bytes.size() - kOffMeshSize;
        std::vector<std::byte> overflow = bytes;
        Patch(overflow, kOffMeshConCountOffset, std::numeric_limits<std::int32_t>::max());
        Patch(overflow, kOffMeshBaseOffset, std::numeric_limits<std::int32_t>::max());
        Require(!KCDC::Features::Navigation::TileCodec::Decode(overflow, ids, decoded, error), "off-mesh counts that overflow an int are refused");

        std::vector<std::byte> unpaired = bytes;
        Patch(unpaired, link + 28, std::uint16_t {0});
        Require(!KCDC::Features::Navigation::TileCodec::Decode(unpaired, ids, decoded, error), "an off-mesh connection not paired with its own polygon is refused");

        const float nan                  = std::numeric_limits<float>::quiet_NaN();
        std::vector<std::byte> nanVertex = bytes;
        Patch(nanVertex, KCDC::Features::Navigation::TileCodec::kHeaderSize + 8 * 4, nan);
        Require(!KCDC::Features::Navigation::TileCodec::Decode(nanVertex, ids, decoded, error), "a NaN vertex is refused");
        std::vector<std::byte> nanBounds = bytes;
        Patch(nanBounds, kBoundsOffset, std::numeric_limits<float>::infinity());
        Require(!KCDC::Features::Navigation::TileCodec::Decode(nanBounds, ids, decoded, error), "infinite bounds are refused");
        std::vector<std::byte> nanLink = bytes;
        Patch(nanLink, link, nan);
        Require(!KCDC::Features::Navigation::TileCodec::Decode(nanLink, ids, decoded, error), "a NaN off-mesh position is refused");
    }

    void QueryTests() {
        KCDC::Features::Navigation::Navmesh navmesh;
        std::map<std::string, KCDC::Features::Navigation::MeshFiles> overlays;
        overlays.emplace("walled_up", Files(Corridor(false)));
        // A quest state that changes no ground ships as an empty pair; it is still an overlay.
        overlays.emplace("unchanged", KCDC::Features::Navigation::MeshFiles {});
        std::vector<std::string> rejected;
        std::string error;
        Require(navmesh.Build(Files(Corridor(true)), std::move(overlays), rejected, error), "the corridor loads");
        Require(rejected.empty() && navmesh.TileCount() == 1 && navmesh.PolygonCount() == 4, "one tile of four polygons");

        const KCDC::Features::Navigation::QueryOptions open;
        const glm::vec3 inA(1.0F, 8.0F, 0.0F);
        const glm::vec3 inB(15.0F, 8.0F, 0.0F);

        const auto closest = navmesh.ClosestPoint(glm::vec3(1.0F, 8.0F, 3.0F), open);
        Require(closest && Near(closest->z, 0.0F) && Near(closest->x, 1.0F) && Near(closest->y, 8.0F), "a point above the floor snaps down onto it");
        Require(!navmesh.ClosestPoint(glm::vec3(1.0F, 20.0F, 0.0F), open), "a point beyond the search radius is off the mesh");
        const auto floor = navmesh.FloorAt(glm::vec3(4.0F, 8.0F, 0.5F), open);
        Require(floor && Near(*floor, 0.0F), "the floor height is read off the polygon");

        const auto path = navmesh.FindPath(inA, inB, open);
        Require(path && path->complete && path->points.size() >= 2, "the rooms are connected through the doorway");
        Require(glm::distance(path->points.front(), inA) < 0.1F && glm::distance(path->points.back(), inB) < 0.1F, "the path runs from start to goal");

        const auto clear = navmesh.Raycast(inA, inB, open);
        Require(clear && !clear->hit && glm::distance(clear->point, inB) < 0.1F, "a straight walk down the corridor reaches its end");
        const auto wall = navmesh.Raycast(inA, glm::vec3(1.0F, 20.0F, 0.0F), open);
        Require(wall && wall->hit && Near(wall->point.y, 10.0F, 0.1F), "a walk into the corridor's wall stops at it");

        const auto anywhere = navmesh.RandomPoint(open);
        Require(anywhere && anywhere->y >= 6.0F - 0.01F && anywhere->y <= 10.0F + 0.01F && anywhere->x >= -0.01F && anywhere->x <= 16.01F, "a point anywhere lies on the mesh");

        const auto random = navmesh.RandomPoint(inA, 30.0F, open);
        Require(random && random->y >= 6.0F - 0.01F && random->y <= 10.0F + 0.01F, "a random point lies on the corridor");

        // A shut door refuses its doorway polygon, not only its off-mesh link.
        const fu2::function<bool(const KCDC::Features::Navigation::EntityLink &) const> locked = [](const KCDC::Features::Navigation::EntityLink &link) { return link.entityGuid != kDoorGuid; };
        KCDC::Features::Navigation::QueryOptions shut;
        shut.linkGate     = &locked;
        const auto barred = navmesh.FindPath(inA, inB, shut);
        Require(barred && !barred->complete, "a locked door cuts the corridor");
        Require(barred->points.back().x <= 7.0F + 0.05F, "the barred path ends at the door");

        Require(navmesh.OverlayNames() == std::vector<std::string> {"unchanged", "walled_up"}, "every overlay is listed, sorted");
        Require(navmesh.EnableOverlay("unchanged", error) && navmesh.PolygonCount() == 4 && navmesh.DisableOverlay("unchanged", error), "an empty overlay enables and changes nothing");
        Require(navmesh.EnableOverlay("walled_up", error) && !navmesh.EnableOverlay("walled_up", error) && error.empty(), "an overlay enables once");
        Require(navmesh.TileCount() == 1 && navmesh.PolygonCount() == 2, "the overlay replaces the column");
        const auto walled = navmesh.FindPath(inA, inB, open);
        Require(walled && !walled->complete, "the overlay's wall cuts the corridor");
        Require(navmesh.DisableOverlay("walled_up", error) && !navmesh.DisableOverlay("walled_up", error) && error.empty(), "an overlay disables once");
        const auto reopened = navmesh.FindPath(inA, inB, open);
        Require(reopened && reopened->complete && navmesh.PolygonCount() == 4, "disabling the overlay gives the base mesh back");
        const auto relocked = navmesh.FindPath(inA, inB, shut);
        Require(relocked && !relocked->complete, "the restored doorway is tied to its door again");

        Require(!navmesh.EnableOverlay("no_such_overlay", error) && error.empty(), "an unknown overlay is refused");

        // One polygon of search is not enough to cross the corridor; the default budget is.
        KCDC::Features::Navigation::QueryOptions hurried;
        hurried.maxSearchNodes = 1;
        const auto cut         = navmesh.FindPath(inA, inB, hurried);
        Require(cut && !cut->complete, "a search out of budget returns a partial path");
        hurried.maxSearchNodes = 0;
        Require(navmesh.FindPath(inA, inA + glm::vec3(1.0F, 0.0F, 0.0F), hurried).has_value(), "a budget below one still searches the start polygon");

        // Detour draws from every polygon the circle touches; anything past the radius is drawn again.
        bool landed = false;
        for (int i = 0; i < 50; ++i) {
            const auto drawn = navmesh.RandomPoint(inA, 1.5F, open);
            if (drawn) {
                Require(glm::distance(glm::vec2(*drawn), glm::vec2(inA)) <= 1.5F + 0.01F, "a random point lies within its radius");
                landed = true;
            }
        }
        Require(landed, "a random point within a small radius is found");
    }

    /** Serves overlay tiles from memory, counting its reads and failing them on demand. */
    class CountingReader final: public KCDC::Features::Navigation::OverlayReader {
      public:
        std::map<std::string, std::vector<std::byte>> tiles;
        mutable int reads = 0;
        bool failing      = false;

        bool ReadTiles(const std::string &name, std::vector<std::byte> &out, std::string &error) const override {
            ++reads;
            if (failing || !tiles.contains(name)) {
                error = "unreadable";
                return false;
            }
            out = tiles.at(name);
            return true;
        }
    };

    KCDC::Features::Navigation::OverlayIndex IndexOf(const KCDC::Features::Navigation::MeshFiles &files) {
        return KCDC::Features::Navigation::OverlayIndex {files.index, files.tiles.size()};
    }

    void OverlayTests() {
        std::vector<std::string> rejected;
        std::string error;

        // A base mesh that fails after the overlays are indexed leaves nothing behind.
        GameTileWriter broken = Corridor(true);
        broken.version        = 7;
        KCDC::Features::Navigation::Navmesh failed;
        std::map<std::string, KCDC::Features::Navigation::MeshFiles> one;
        one.emplace("walled_up", Files(Corridor(false)));
        Require(!failed.Build(Files(broken), std::move(one), rejected, error) && !error.empty(), "a base mesh whose tiles are broken fails the build");
        Require(!failed.IsReady() && failed.OverlayNames().empty() && failed.ActiveOverlays().empty(), "a failed build keeps no overlays");

        auto reader                                         = std::make_unique<CountingReader>();
        CountingReader &counting                            = *reader;
        const KCDC::Features::Navigation::MeshFiles walled  = Files(Corridor(false));
        const KCDC::Features::Navigation::MeshFiles doorway = Files(Corridor(true));
        KCDC::Features::Navigation::MeshFiles corrupt       = Files(broken);
        counting.tiles.emplace("walled_up", walled.tiles);
        counting.tiles.emplace("doorway", doorway.tiles);
        counting.tiles.emplace("corrupt", corrupt.tiles);
        // Tiles shorter than the index was checked against.
        counting.tiles.emplace("shrunk", std::vector<std::byte>(walled.tiles.begin(), walled.tiles.end() - 4));
        std::map<std::string, KCDC::Features::Navigation::OverlayIndex> overlays;
        overlays.emplace("walled_up", IndexOf(walled));
        overlays.emplace("doorway", IndexOf(doorway));
        overlays.emplace("corrupt", IndexOf(corrupt));
        overlays.emplace("shrunk", IndexOf(walled));
        overlays.emplace("garbled", KCDC::Features::Navigation::OverlayIndex {std::vector<std::byte>(3), 0});

        KCDC::Features::Navigation::Navmesh navmesh;
        rejected.clear();
        Require(navmesh.Build(Files(Corridor(true)), std::move(overlays), std::move(reader), rejected, error), "the corridor loads with lazy overlays");
        Require(rejected.size() == 1 && rejected.front().starts_with("garbled"), "an overlay whose index is malformed is rejected at load");
        Require(counting.reads == 0, "no overlay tiles are read at load");
        Require(navmesh.OverlayNames() == std::vector<std::string> {"corrupt", "doorway", "shrunk", "walled_up"}, "the indexed overlays are listed");

        Require(!navmesh.EnableOverlay("corrupt", error) && !error.empty(), "an overlay with broken tiles is refused when enabled");
        Require(!navmesh.EnableOverlay("shrunk", error) && !error.empty(), "an overlay whose tiles do not match its index is refused");
        Require(navmesh.ActiveOverlays().empty() && navmesh.PolygonCount() == 4, "a refused overlay leaves the mesh as it was");

        Require(navmesh.EnableOverlay("doorway", error) && navmesh.EnableOverlay("walled_up", error) && navmesh.PolygonCount() == 2, "the latest overlay wins its column");
        Require(navmesh.DisableOverlay("walled_up", error) && navmesh.PolygonCount() == 4, "disabling it reads the overlay underneath again");
        Require(navmesh.EnableOverlay("walled_up", error), "the overlay enables again");
        counting.failing = true;
        Require(!navmesh.DisableOverlay("walled_up", error) && !error.empty(), "a column whose overlay cannot be read again is reported");
        Require(navmesh.ActiveOverlays() == std::vector<std::string> {"doorway"} && navmesh.PolygonCount() == 0, "the overlay is disabled and the column left empty");
        counting.failing = false;
        Require(navmesh.DisableOverlay("doorway", error) && navmesh.PolygonCount() == 4, "the base mesh's covered column is kept to give back");
    }

    void WritePak(const std::filesystem::path &path, const std::vector<std::pair<std::string, std::vector<std::byte>>> &entries) {
        std::filesystem::create_directories(path.parent_path());
        mz_zip_archive zip {};
        Require(mz_zip_writer_init_file(&zip, path.string().c_str(), 0), "the pak fixture opens for writing");
        for (const auto &[name, bytes] : entries) {
            Require(mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_COMPRESSION), "the pak fixture takes an entry");
        }
        Require(mz_zip_writer_finalize_archive(&zip), "the pak fixture finalizes");
        mz_zip_writer_end(&zip);
    }

    void ServiceTests() {
        const PakFixture fixture;
        const KCDC::Features::Navigation::MeshFiles base   = Files(Corridor(true));
        const KCDC::Features::Navigation::MeshFiles walled = Files(Corridor(false));
        const std::filesystem::path pak                    = fixture.Path("levels/recast.pak");
        WritePak(pak, {{"recast/rdnavmesh_1.hdr", base.index}, {"recast/rdnavmesh_1.nav", base.tiles}, {"recast/rdnavmesh_walled_up_1.hdr", walled.index}, {"recast/rdnavmesh_walled_up_1.nav", walled.tiles}, {"recast/rdnavmesh_unchanged_1.hdr", {}},
                          {"recast/rdnavmesh_unchanged_1.nav", {}}, {"recast/rdnavmesh_half_1.hdr", walled.index}, {"recast/rdnavmesh_walled_up_2.nav", walled.tiles}, {"recast/notes.txt", {}}, {"objects/rdnavmesh_1.nav", walled.tiles}});

        KCDC::Features::Navigation::MeshFiles readBase;
        std::map<std::string, KCDC::Features::Navigation::OverlayIndex> overlays;
        std::string error;
        Require(KCDC::Features::Navigation::NavigationService::ReadPak(pak.string(), readBase, overlays, error), "the pak reads");
        Require(readBase.index == base.index && readBase.tiles == base.tiles, "the base pair comes out whole");
        Require(overlays.size() == 2 && overlays.contains("walled_up") && overlays.contains("unchanged"), "overlays are named from their entries; a half pair or another suffix is not one");
        Require(overlays.at("walled_up").index == walled.index && overlays.at("walled_up").tilesSize == walled.tiles.size(), "an overlay keeps its index and the length of its tiles");
        Require(overlays.at("unchanged").index.empty() && overlays.at("unchanged").tilesSize == 0, "an empty pair is still an overlay");

        const std::filesystem::path baseless = fixture.Path("levels/baseless.pak");
        WritePak(baseless, {{"recast/rdnavmesh_1.hdr", base.index}, {"recast/rdnavmesh_walled_up_1.hdr", walled.index}, {"recast/rdnavmesh_walled_up_1.nav", walled.tiles}});
        overlays.clear();
        Require(!KCDC::Features::Navigation::NavigationService::ReadPak(baseless.string(), readBase, overlays, error) && !error.empty(), "a pak without the base pair is refused");
        std::ofstream(fixture.Path("levels/garbage.pak")) << "not a zip";
        Require(!KCDC::Features::Navigation::NavigationService::ReadPak(fixture.Path("levels/garbage.pak").string(), readBase, overlays, error), "a file that is not a zip is refused");

        KCDC::Shared::Entities::DoorEntity door;
        door.doorGuid = kDoorGuid;
        bool known    = true;
        KCDC::Features::Navigation::NavigationService service;
        service.Install("kutnohorsko", pak.string(), [&door, &known](std::uint64_t guid) -> const KCDC::Shared::Entities::DoorEntity * {
            return known && guid == door.doorGuid ? &door : nullptr;
        });
        Require(service.IsReady() && service.GetSourcePath() == std::filesystem::canonical(pak).string(), "the service loads the configured pak");
        Require(service.GetMesh().OverlayNames() == std::vector<std::string> {"unchanged", "walled_up"}, "the service lists the pak's overlays");
        Require(service.EnableOverlay("walled_up") && service.GetMesh().PolygonCount() == 2, "an overlay is read out of the pak when enabled");
        Require(service.DisableOverlay("walled_up") && service.GetMesh().PolygonCount() == 4, "and disabled again");
        Require(!service.EnableOverlay("half") && !service.DisableOverlay("walled_up"), "a half pair cannot be enabled, nor a disabled overlay disabled");

        const glm::vec3 inA(1.0F, 8.0F, 0.0F);
        const glm::vec3 inB(15.0F, 8.0F, 0.0F);
        const auto crosses = [&service, &inA, &inB](KCDC::Features::Navigation::DoorPolicy policy) {
            const auto path = service.GetMesh().FindPath(inA, inB, service.Options(policy, 2.0F, 4.0F));
            return path && path->complete;
        };
        const auto doorPath = service.GetMesh().FindPath(inA, inB, service.Options(KCDC::Features::Navigation::DoorPolicy::Any, 2.0F, 4.0F, 65535));
        Require(doorPath && doorPath->doors.size() == 1 && doorPath->doors.front().guid == kDoorGuid, "straight surface paths retain the native doorway exactly once");
        door.locked = true;
        door.open   = false;
        Require(!crosses(KCDC::Features::Navigation::DoorPolicy::Unlocked) && !crosses(KCDC::Features::Navigation::DoorPolicy::Open), "a locked door bars unlocked and open queries");
        Require(crosses(KCDC::Features::Navigation::DoorPolicy::Any) && !crosses(KCDC::Features::Navigation::DoorPolicy::None), "any ignores the lock; none bars every door");
        door.locked = false;
        Require(crosses(KCDC::Features::Navigation::DoorPolicy::Unlocked) && !crosses(KCDC::Features::Navigation::DoorPolicy::Open), "a shut unlocked door passes unlocked queries only");
        door.open = true;
        Require(crosses(KCDC::Features::Navigation::DoorPolicy::Open), "an open door passes open queries");
        known     = false;
        door.open = false;
        Require(crosses(KCDC::Features::Navigation::DoorPolicy::Open) && crosses(KCDC::Features::Navigation::DoorPolicy::Unlocked), "a door the server does not know is not gated");

        const KCDC::Features::Navigation::EntityLink climb {kDoorGuid, 3};
        Require(KCDC::Features::Navigation::NavigationService::MayCross(KCDC::Features::Navigation::DoorPolicy::None, climb, nullptr), "a link that is not a door is crossed under every policy");
        const KCDC::Features::Navigation::QueryOptions budget = service.Options(KCDC::Features::Navigation::DoorPolicy::Unlocked, 2.0F, 4.0F, 7);
        Require(budget.maxSearchNodes == 7 && service.DefaultOptions().maxSearchNodes == KCDC::Features::Navigation::Navmesh::kMaxSearchNodes, "options carry the search budget");
    }

    void UnmarkedDoorTests() {
        // A door whose leaf crosses the middle polygon, and a second door in the
        // same doorway, as the level places variants of one door at one spot.
        constexpr std::uint64_t kUnmarkedGuid = 0x00000000DEADBEEFULL;
        constexpr std::uint64_t kVariantGuid  = 0x00000000FEEDFACEULL;
        constexpr std::uint64_t kElsewhere    = 0x0000000012345678ULL;
        const KCDC::Features::Navigation::Navmesh::DoorLeaf leaf {kUnmarkedGuid, glm::vec3(8.0F, 7.4F, 0.0F), glm::vec3(8.0F, 8.6F, 0.0F)};
        const KCDC::Features::Navigation::Navmesh::DoorLeaf variant {kVariantGuid, glm::vec3(8.0F, 7.4F, 0.0F), glm::vec3(8.0F, 8.6F, 0.0F)};
        const KCDC::Features::Navigation::Navmesh::DoorLeaf storeyUp {kElsewhere, glm::vec3(4.0F, 7.0F, 3.0F), glm::vec3(4.0F, 9.0F, 3.0F)};

        KCDC::Features::Navigation::Navmesh navmesh;
        navmesh.SetDoorLeaves({leaf, variant, storeyUp});
        std::vector<std::string> rejected;
        std::string error;
        Require(navmesh.Build(Files(UnmarkedCorridor()), std::map<std::string, KCDC::Features::Navigation::MeshFiles> {}, rejected, error), "the unmarked corridor loads");
        Require(navmesh.SyntheticDoorCount() == 2 && navmesh.SyntheticDoorwayCount() == 1, "both doors in the doorway gate the one polygon under their leaf; a leaf a storey up marks nothing");

        KCDC::Shared::Entities::DoorEntity door;
        door.doorGuid = kUnmarkedGuid;
        KCDC::Shared::Entities::DoorEntity other;
        other.doorGuid = kVariantGuid;
        const auto find = [&door, &other](std::uint64_t guid) -> const KCDC::Shared::Entities::DoorEntity * {
            return guid == door.doorGuid ? &door : (guid == other.doorGuid ? &other : nullptr);
        };
        const auto gate = [&find](KCDC::Features::Navigation::DoorPolicy policy) {
            return fu2::function<bool(const KCDC::Features::Navigation::EntityLink &) const>(
                [policy, &find](const KCDC::Features::Navigation::EntityLink &link) { return KCDC::Features::Navigation::NavigationService::MayCross(policy, link, link.kind == KCDC::Features::Navigation::TileCodec::kDoorLinkKind ? find(link.entityGuid) : nullptr); });
        };
        const glm::vec3 inA(1.0F, 8.0F, 0.0F);
        const glm::vec3 inB(15.0F, 8.0F, 0.0F);
        const auto crosses = [&](KCDC::Features::Navigation::DoorPolicy policy) {
            const fu2::function<bool(const KCDC::Features::Navigation::EntityLink &) const> rule = gate(policy);
            KCDC::Features::Navigation::QueryOptions options;
            options.linkGate = policy == KCDC::Features::Navigation::DoorPolicy::Any ? nullptr : &rule;
            const auto path  = navmesh.FindPath(inA, inB, options);
            return path && path->complete;
        };

        const auto sharedPath = navmesh.FindPath(inA, inB, {});
        Require(sharedPath && sharedPath->doors.size() == 2, "a straight path retains every leaf sharing a synthetic doorway");
        const auto beforeDoor = navmesh.FindPath(inA, glm::vec3(4, 8, 0), {});
        Require(beforeDoor && beforeDoor->complete && beforeDoor->doors.empty(), "nearby doors outside the final path are not traversal actions");
        door.open  = false;
        other.open = false;
        Require(!crosses(KCDC::Features::Navigation::DoorPolicy::Open), "a shut door the mesh marks no doorway for still bars an open-doors route");
        {
            const fu2::function<bool(const KCDC::Features::Navigation::EntityLink &) const> rule = gate(KCDC::Features::Navigation::DoorPolicy::Open);
            KCDC::Features::Navigation::QueryOptions options;
            options.linkGate  = &rule;
            const auto barred = navmesh.FindPath(inA, inB, options);
            Require(barred && !barred->complete && barred->points.back().x <= 7.0F + 0.05F, "the route is partial and ends at the doorway, so a move planned on it is blocked at plan time");
            KCDC::Features::Npc::NpcNavigation navigation;
            KCDC::Shared::Game::NpcIntent goal;
            goal.kind  = KCDC::Shared::Game::NpcIntentKind::MoveTo;
            goal.point = inB;
            navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Server);
            navigation.Plan(navmesh, options, inA);
            Require(navigation.IsPlanned() && !navigation.IsComplete(), "an NPC route through it is planned partial, never through the leaf");
        }
        Require(crosses(KCDC::Features::Navigation::DoorPolicy::Unlocked) && crosses(KCDC::Features::Navigation::DoorPolicy::Any) && !crosses(KCDC::Features::Navigation::DoorPolicy::None), "unlocked, any and none read it as they read any door");
        door.open = true;
        Require(!crosses(KCDC::Features::Navigation::DoorPolicy::Open), "one open door is not enough while the other in the same doorway is shut");
        other.open = true;
        Require(crosses(KCDC::Features::Navigation::DoorPolicy::Open), "with both open the route passes");
        door.locked = true;
        Require(!crosses(KCDC::Features::Navigation::DoorPolicy::Unlocked) && crosses(KCDC::Features::Navigation::DoorPolicy::Any), "a lock on either bars unlocked queries; any ignores it");

        // A door the mesh does name keeps its own doorway and gets no second one.
        KCDC::Features::Navigation::Navmesh linked;
        linked.SetDoorLeaves({KCDC::Features::Navigation::Navmesh::DoorLeaf {kDoorGuid, glm::vec3(8.0F, 7.4F, 0.0F), glm::vec3(8.0F, 8.6F, 0.0F)}});
        Require(linked.Build(Files(Corridor(true)), std::map<std::string, KCDC::Features::Navigation::MeshFiles> {}, rejected, error) && linked.SyntheticDoorCount() == 0 && linked.SyntheticDoorwayCount() == 0, "a door with its own link is left to it");

        // The catalog's leaves: pivot, facing and the side the model is named for.
        const std::vector<KCDC::Features::Navigation::Navmesh::DoorLeaf> leaves = KCDC::Features::Navigation::NavigationService::DoorLeaves("kutnohorsko");
        const auto right21 = std::find_if(leaves.begin(), leaves.end(), [](const KCDC::Features::Navigation::Navmesh::DoorLeaf &candidate) {
            return candidate.entityGuid == 0x042e80199d13e183ULL;
        });
        Require(right21 != leaves.end() && Near(glm::distance(right21->from, right21->to), KCDC::Features::Navigation::NavigationService::kLeafWidth) && right21->to.x > right21->from.x + 1.0F, "a right-hand door facing south swings its leaf east of its pivot, across the route the stair landing traced");
    }

    void PatrolValidationTests() {
        KCDC::Shared::Game::PatrolRouteDefinition route;
        route.id     = "test";
        route.points = {{{1, 8, 0}}, {{15, 8, 0}}, {{1, 9, 0}}};
        KCDC::Features::Navigation::Navmesh mesh;
        const KCDC::Features::Navigation::QueryOptions options;
        Require(!KCDC::Features::Patrol::ValidatePatrol(route, mesh, options).available, "missing mesh is unavailable");
        std::vector<std::string> rejected;
        std::string error;
        Require(mesh.Build(Files(Corridor(true)), {}, rejected, error), "patrol mesh loads");
        auto result = KCDC::Features::Patrol::ValidatePatrol(route, mesh, options);
        Require(result.available && result.complete && result.legs.size() == 3 && result.legs.back().toIndex == 0, "loop validates closing leg");
        route.mode = KCDC::Shared::Game::PatrolMode::PingPong;
        result     = KCDC::Features::Patrol::ValidatePatrol(route, mesh, options);
        Require(result.complete && result.legs.size() == 4 && result.legs[2].fromIndex == 2 && result.legs[2].toIndex == 1 && result.legs[3].toIndex == 0, "ping-pong validates both directions");
        route.mode = KCDC::Shared::Game::PatrolMode::Once;
        Require(KCDC::Features::Patrol::ValidatePatrol(route, mesh, options).legs.size() == 2, "once omits closing leg");
        route.points[1].position = {999, 999, 999};
        Require(!KCDC::Features::Patrol::ValidatePatrol(route, mesh, options).complete, "off-mesh point is incomplete");
        route.points.resize(1);
        Require(KCDC::Features::Patrol::ValidatePatrol(route, mesh, options).complete, "single waypoint validated");
        route.points = {{{1, 8, 0}}, {{15, 8, 0}}};
        Require(mesh.Build(Files(Corridor(false)), {}, rejected, error), "disconnected patrol mesh loads");
        result = KCDC::Features::Patrol::ValidatePatrol(route, mesh, options);
        Require(result.available && !result.complete, "partial paths are not complete");
    }

    void NpcNavigationTests() {
        KCDC::Features::Navigation::Navmesh mesh;
        std::vector<std::string> rejected;
        std::string error;
        Require(mesh.Build(Files(Corridor(true)), {}, rejected, error), "the NPC test mesh loads");
        const KCDC::Features::Navigation::QueryOptions options;
        const glm::vec3 start(1.0F, 8.0F, 0.0F);
        KCDC::Shared::Game::NpcIntent goal;
        goal.kind  = KCDC::Shared::Game::NpcIntentKind::MoveTo;
        goal.point = glm::vec3(15.0F, 8.0F, 0.0F);
        KCDC::Features::Npc::NpcNavigation navigation;

        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Auto);
        Require(navigation.UsesServer(true, true) && navigation.UsesServer(false, true), "auto routes a move on the server whenever a mesh is loaded, streamed or not");
        Require(!navigation.UsesServer(true, false) && navigation.UsesServer(false, false), "without a mesh auto steers a streamed body natively and dead-reckons a dormant one");
        KCDC::Shared::Game::NpcIntent follow = goal;
        follow.kind                          = KCDC::Shared::Game::NpcIntentKind::Follow;
        navigation.Start(follow, KCDC::Features::Npc::Pathfinding::Auto);
        Require(!navigation.UsesServer(true, true), "a follow chases a moving point natively until it blocks");
        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Game);
        Require(!navigation.UsesServer(false, true) && !navigation.UsesServer(true, true), "game mode steers natively and waits for a simulator");
        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Server);
        Require(navigation.UsesServer(false, true) && navigation.UsesServer(true, true), "server planning applies to streamed and dormant bodies");
        navigation.Plan(mesh, options, start);
        Require(navigation.IsPlanned() && navigation.HasCorner() && navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Running, "a server move starts on its mesh path");
        Require(navigation.Goal() == goal && glm::distance(navigation.Corner(), goal.point) < 0.1F, "the original destination survives corner steering");
        glm::vec3 position = start;
        navigation.Advance(position, 3.0F);
        Require(Near(position.x, 4.0F) && Near(position.z, 0.0F) && navigation.HasCorner(), "a dormant body advances at the requested pace on the floor");
        navigation.Advance(position, 20.0F);
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Reached && glm::distance(position, goal.point) < 0.1F, "a complete path finishes at the goal");
        navigation.Invalidate();
        Require(!navigation.IsPlanned() && navigation.Goal() == goal && navigation.Mode() == KCDC::Features::Npc::Pathfinding::Server, "replanning keeps the original goal and planner");

        position                     = goal.point - glm::vec3(1.0F, 0.0F, 0.0F);
        const glm::vec3 withinRadius = position;
        navigation.Plan(mesh, options, position);
        navigation.Advance(position, 0.1F);
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Reached && position == withinRadius, "a complete move respects its arrival radius without moving closer");

        const fu2::function<bool(const KCDC::Features::Navigation::EntityLink &) const> locked = [](const auto &) { return false; };
        KCDC::Features::Navigation::QueryOptions shut;
        shut.linkGate = &locked;
        navigation.Plan(mesh, shut, start);
        Require(Near(navigation.CornerRadius(), KCDC::Features::Npc::NpcNavigation::kBendRadius), "a partial path never uses the final goal's arrival radius");
        navigation.Advance(position = start, 20.0F);
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Blocked && position.x < 7.1F, "a locked door blocks a server move at the doorway");
        navigation.Advance(position, 20.0F);
        Require(position.x < 7.1F && navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Blocked, "a blocked move stays blocked instead of walking through the door");

        goal.point = glm::vec3(500.0F);
        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Auto);
        navigation.Plan(mesh, options, start);
        Require(!navigation.HasCorner() && navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Blocked, "off-mesh endpoints never become a straight-line fallback with a loaded mesh");

        KCDC::Features::Navigation::Navmesh bend;
        Require(bend.Build(Files(Bend()), {}, rejected, error), "a corridor with a bend loads");
        goal.point = glm::vec3(9.0F, 9.0F, 0.0F);
        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Server);
        const glm::vec3 bendStart(1.0F, 1.0F, 0.0F);
        navigation.Plan(bend, options, bendStart);
        Require(navigation.HasCorner() && glm::distance(navigation.Corner(), goal.point) > 1.0F, "a streamed body is sent to an intermediate corner instead of straight to the destination");
        navigation.AdvanceCorner();
        Require(navigation.HasCorner() && navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Running && navigation.Goal() == goal, "an intermediate arrival keeps the whole move running");
        while (navigation.HasCorner()) {
            navigation.AdvanceCorner();
        }
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Reached, "only the final corner completes a streamed move");
        navigation.Invalidate();
        navigation.Plan(bend, options, bendStart);
        position = bendStart;
        navigation.Advance(position, 3.0F);
        Require(position.x > bendStart.x && position.y < 2.1F, "dormant movement follows the bend instead of crossing its wall");
        navigation.Advance(position, 30.0F);
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Reached && glm::distance(position, goal.point) < 0.1F, "dormant and streamed moves share the same corners and final goal");

        // Server mode is refused without a mesh before it is accepted
        // (`Npcs::RequireMesh`); planning itself never throws.
        const KCDC::Features::Navigation::Navmesh empty;
        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Auto);
        navigation.Plan(empty, options, start);
        Require(navigation.HasCorner() && navigation.Corner() == goal.point, "auto preserves the old dormant fallback only without a mesh");
        Require(navigation.SimulatorIntent(start).point == goal.point && Near(navigation.SimulatorIntent(start).radius, goal.radius), "a straight line hands a simulator the goal itself");
    }

    void EmptyTests() {
        const KCDC::Features::Navigation::Navmesh navmesh;
        const KCDC::Features::Navigation::QueryOptions options;
        Require(!navmesh.IsReady(), "a mesh that was never built is not ready");
        Require(!navmesh.ClosestPoint(glm::vec3(0.0F), options) && !navmesh.FindPath(glm::vec3(0.0F), glm::vec3(1.0F), options) && !navmesh.Raycast(glm::vec3(0.0F), glm::vec3(1.0F), options), "every query answers empty without a mesh");
        Require(navmesh.OverlayNames().empty(), "no mesh, no overlays");
    }

    void NpcFallbackTests() {
        KCDC::Features::Navigation::Navmesh mesh;
        std::vector<std::string> rejected;
        std::string error;
        Require(mesh.Build(Files(Bend()), {}, rejected, error), "the fallback test mesh loads");
        const KCDC::Features::Navigation::QueryOptions options;
        const glm::vec3 start(1.0F, 1.0F, 0.0F);
        const glm::vec3 destination(9.0F, 9.0F, 0.0F);
        KCDC::Shared::Game::NpcIntent goal;
        goal.point  = destination;
        goal.radius = 1.5F;
        goal.speed  = KCDC::Shared::Game::NpcSpeed::Jog;
        KCDC::Features::Npc::NpcNavigation navigation;

        // What the order does on a blocked report: switch, then plan when the
        // queue reaches it.
        const auto fallback = [&](const KCDC::Features::Navigation::Navmesh &on, const glm::vec3 &to) {
            if (!navigation.StartFallback(to, on.IsReady())) {
                return false;
            }
            navigation.Plan(on, options, start);
            return true;
        };

        for (const auto kind : {KCDC::Shared::Game::NpcIntentKind::MoveTo, KCDC::Shared::Game::NpcIntentKind::Follow, KCDC::Shared::Game::NpcIntentKind::Flee}) {
            goal.kind            = kind;
            goal.point           = kind == KCDC::Shared::Game::NpcIntentKind::MoveTo ? destination : start;
            goal.targetNetworkId = kind == KCDC::Shared::Game::NpcIntentKind::Follow ? 123U : 0U;
            for (const auto mode : {KCDC::Features::Npc::Pathfinding::Auto, KCDC::Features::Npc::Pathfinding::Game}) {
                navigation.Start(goal, mode);
                Require(fallback(mesh, destination), "every game movement order can fall back to a server route");
                Require(navigation.Goal() == goal && navigation.UsesServer(true, true) && navigation.UsesServer(false, true), "fallback preserves the order across streaming changes");
                const auto step = navigation.SimulatorIntent(start);
                Require(step.kind == KCDC::Shared::Game::NpcIntentKind::MoveTo && step.speed == goal.speed && step.point == navigation.Corner(), "the simulator walks server corners using the requested pace");
                Require(glm::distance(step.point, destination) > 1.0F, "fallback goes around the bend instead of walking into its wall");
                Require(!fallback(mesh, destination), "a blocked server route is not retried");
                while (navigation.HasCorner()) {
                    Require(kind != KCDC::Shared::Game::NpcIntentKind::Flee || navigation.CornerRadius() <= 0.5F, "flee recovery reaches its safe destination instead of using the flee distance as an arrival radius");
                    navigation.AdvanceCorner();
                }
                Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Reached, "only the completed server route reports reached");
                navigation.Start(goal, mode);
                Require(navigation.SimulatorIntent(start) == goal && fallback(mesh, destination), "a new order cancels the old fallback and can recover again");
            }
        }

        const KCDC::Features::Navigation::Navmesh empty;
        goal.kind = KCDC::Shared::Game::NpcIntentKind::MoveTo;
        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Game);
        Require(!fallback(empty, destination) && navigation.Mode() == KCDC::Features::Npc::Pathfinding::Game && !navigation.IsPlanned(), "no mesh leaves the game result unchanged without throwing");
        Require(fallback(mesh, glm::vec3(500.0F)), "an unavailable endpoint still attempts the loaded server mesh");
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Blocked && !fallback(mesh, destination), "an unavailable server path ends blocked without repeated planning");
        goal.kind = KCDC::Shared::Game::NpcIntentKind::Hold;
        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Auto);
        Require(!fallback(mesh, destination), "standing orders never start fallback movement");

        KCDC::Features::Navigation::Navmesh corridor;
        Require(corridor.Build(Files(Corridor(true)), {}, rejected, error), "the flee test corridor loads");
        goal.kind   = KCDC::Shared::Game::NpcIntentKind::Flee;
        goal.point  = glm::vec3(1.0F, 8.0F, 0.0F);
        goal.radius = 15.5F;
        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Auto);
        Require(navigation.StartFallback(goal.FleeDestination(goal.point), true), "flee falls back to the destination just past the mesh boundary");
        navigation.Plan(corridor, options, goal.point);
        Require(navigation.HasCorner(), "the flee endpoint snaps onto the corridor");
        while (navigation.HasCorner()) {
            navigation.AdvanceCorner();
        }
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Blocked, "snapping a flee endpoint too close to the threat never reports a safe arrival");

        // The corridor ends 15 m from the threat. A flee radius of 14.8 is
        // met there -- the same separation the simulator counts as arrival --
        // even though the snapped end is short of the projected destination.
        goal.radius = 14.8F;
        navigation.Start(goal, KCDC::Features::Npc::Pathfinding::Auto);
        Require(navigation.StartFallback(goal.FleeDestination(goal.point), true), "a shorter flee falls back too");
        navigation.Plan(corridor, options, goal.point);
        while (navigation.HasCorner()) {
            navigation.AdvanceCorner();
        }
        Require(navigation.Status() == KCDC::Shared::Game::NpcIntentStatus::Reached && goal.HasArrived(glm::vec3(16.0F, 8.0F, 0.0F), glm::vec3(0.0F)), "a route ending at the flee radius is an escape, as the simulator measures it");
    }
} // namespace

int RunNavigationSuite() {
    try {
        DiscoveryTests();
        CodecTests();
        QueryTests();
        OverlayTests();
        ServiceTests();
        UnmarkedDoorTests();
        EmptyTests();
        NpcNavigationTests();
        PatrolValidationTests();
        NpcFallbackTests();
        std::cout << "Navigation tests passed\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "Navigation tests failed: " << error.what() << '\n';
        return 1;
    }
}
