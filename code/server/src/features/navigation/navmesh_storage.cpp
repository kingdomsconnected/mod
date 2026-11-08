#include "navmesh.h"
#include "shared/game/door_catalog.h"
#include <algorithm>
#include <cctype>
#include <miniz.h>
#include <string_view>

namespace KCDC::Features::Navigation {
    namespace {
        // `recast/rdnavmesh_1.nav` is the level as it ships; `recast/rdnavmesh_<name>_1.nav` is an overlay.
        constexpr std::string_view kEntryPrefix = "recast/rdnavmesh_";
        constexpr std::string_view kEntrySuffix = "_1";

        /** Extracts one entry whole. False, with `error` set, when the archive will not give it up. */
        bool Extract(mz_zip_archive &zip, mz_uint entry, std::uint64_t size, std::vector<std::byte> &out, std::string &error) {
            out.resize(static_cast<std::size_t>(size));
            if (!out.empty() && !mz_zip_reader_extract_to_mem(&zip, entry, out.data(), out.size(), 0)) {
                error = std::string("could not extract it: ") + mz_zip_get_error_string(mz_zip_get_last_error(&zip));
                return false;
            }
            return true;
        }

        /** Reads an overlay's `.nav` out of the pak it was indexed from, opening the pak for each read. */
        class PakOverlayReader final: public OverlayReader {
          public:
            explicit PakOverlayReader(std::string path): _path(std::move(path)) {}

            bool ReadTiles(const std::string &name, std::vector<std::byte> &tiles, std::string &error) const override {
                mz_zip_archive zip {};
                if (!mz_zip_reader_init_file(&zip, _path.c_str(), 0)) {
                    error = _path + " is no longer a readable pak: " + mz_zip_get_error_string(mz_zip_get_last_error(&zip));
                    return false;
                }
                const std::string entryName = std::string(kEntryPrefix) + name + std::string(kEntrySuffix) + ".nav";
                mz_zip_archive_file_stat stat {};
                const int entry = mz_zip_reader_locate_file(&zip, entryName.c_str(), nullptr, 0);
                const bool read = entry >= 0 && mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(entry), &stat) && Extract(zip, static_cast<mz_uint>(entry), stat.m_uncomp_size, tiles, error);
                if (entry < 0) {
                    error = entryName + " is no longer in " + _path;
                }
                else if (!read && error.empty()) {
                    error = "could not read " + entryName + " in " + _path;
                }
                mz_zip_reader_end(&zip);
                return read;
            }

          private:
            std::string _path;
        };
    } // namespace

    bool Navmesh::ReadPak(const std::string &path, MeshFiles &base, std::map<std::string, OverlayIndex> &overlays, std::string &error) {
        mz_zip_archive zip {};
        if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
            error = std::string("not a readable pak: ") + mz_zip_get_error_string(mz_zip_get_last_error(&zip));
            return false;
        }

        // Which halves of each pair the pak holds: bit 0 the index, bit 1 the tiles. A quest
        // state that adds nothing ships as an empty pair, which is still an overlay.
        std::map<std::string, unsigned> halves;
        const mz_uint count = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat stat {};
            if (!mz_zip_reader_file_stat(&zip, i, &stat) || stat.m_is_directory) {
                continue;
            }
            const std::string_view name(stat.m_filename);
            const bool isIndex = name.ends_with(".hdr");
            if (!name.starts_with(kEntryPrefix) || (!isIndex && !name.ends_with(".nav"))) {
                continue;
            }
            // `1` alone is the base mesh; anything else ending in `_1` is an overlay's name.
            const std::string_view stem = name.substr(kEntryPrefix.size(), name.size() - kEntryPrefix.size() - 4);
            if (stem != "1" && !stem.ends_with(kEntrySuffix)) {
                continue;
            }
            const std::string overlay = stem == "1" ? std::string() : std::string(stem.substr(0, stem.size() - kEntrySuffix.size()));
            halves[overlay] |= isIndex ? 1U : 2U;

            // An overlay's tiles stay in the pak until it is enabled; only their length is needed to check its index.
            std::vector<std::byte> *bytes = nullptr;
            if (overlay.empty()) {
                bytes = isIndex ? &base.index : &base.tiles;
            }
            else if (isIndex) {
                bytes = &overlays[overlay].index;
            }
            else {
                overlays[overlay].tilesSize = stat.m_uncomp_size;
            }
            if (bytes != nullptr && !Extract(zip, i, stat.m_uncomp_size, *bytes, error)) {
                error = std::string(name) + ": " + error;
                mz_zip_reader_end(&zip);
                return false;
            }
        }
        mz_zip_reader_end(&zip);

        // An overlay missing half its pair is not one.
        std::erase_if(overlays, [&halves](const auto &entry) { return halves[entry.first] != 3U; });
        if (halves[std::string()] != 3U || base.index.empty() || base.tiles.empty()) {
            error = "it has no recast/rdnavmesh_1.hdr and .nav";
            return false;
        }
        return true;
    }

    bool Navmesh::LoadPak(const std::string &path, const std::string &level, std::string &error, std::vector<std::string> *rejectedOverlays) {
        MeshFiles base;
        std::map<std::string, OverlayIndex> overlays;
        if (!ReadPak(path, base, overlays, error)) {
            return false;
        }
        SetDoorLeaves(DoorLeaves(level));
        std::vector<std::string> rejected;
        return Build(std::move(base), std::move(overlays), std::make_unique<PakOverlayReader>(path), rejectedOverlays ? *rejectedOverlays : rejected, error);
    }

    std::vector<Navmesh::DoorLeaf> Navmesh::DoorLeaves(const std::string &level) {
        std::vector<Navmesh::DoorLeaf> leaves;
        for (const KCDC::Shared::Game::DoorPlacement &door : KCDC::Shared::Game::g_DoorPlacements) {
            if (level != door.level) {
                continue;
            }
            // The model's own name says which way the leaf swings from its pivot:
            // the last path segment, as `door_village_right21` is.
            std::string model(door.name);
            const std::size_t slash = model.find_last_of('/');
            if (slash != std::string::npos) {
                model = model.substr(slash + 1);
            }
            std::transform(model.begin(), model.end(), model.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const glm::vec3 pivot(door.positionX, door.positionY, door.positionZ);
            const glm::vec3 side(door.forwardY, -door.forwardX, 0.0F);
            Navmesh::DoorLeaf leaf;
            leaf.entityGuid = door.guid;
            if (model.find("left") != std::string::npos) {
                leaf.from = pivot;
                leaf.to   = pivot + side * kDoorLeafWidth;
            }
            else if (model.find("right") != std::string::npos) {
                leaf.from = pivot;
                leaf.to   = pivot - side * kDoorLeafWidth;
            }
            else {
                leaf.from = pivot - side * kDoorLeafWidth;
                leaf.to   = pivot + side * kDoorLeafWidth;
            }
            leaves.push_back(leaf);
        }
        return leaves;
    }

} // namespace KCDC::Features::Navigation
