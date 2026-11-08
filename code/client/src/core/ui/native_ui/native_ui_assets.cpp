#include "native_ui_assets.h"

#include <utils/safe_win32.h>
#include <utils/vfs.h>

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace KCDC::Core {
    namespace {
        constexpr int kMinLayer             = 1;
        constexpr int kMaxLayer             = 100;
        constexpr std::size_t kMaxAssets    = 64;
        constexpr std::size_t kMaxMovieSize = 32u * 1024 * 1024;
        constexpr std::size_t kMaxBookImageSize = 16u * 1024 * 1024;

        std::string Sanitize(const std::string &value, std::size_t limit) {
            std::string out;
            out.reserve(value.size() < limit ? value.size() : limit);
            for (char character : value) {
                if (out.size() >= limit) {
                    break;
                }
                const bool allowed = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_';
                out.push_back(allowed ? character : '_');
            }
            return out;
        }

        std::uint64_t Hash(const std::string &value, std::uint64_t seed) {
            std::uint64_t hash = seed;
            for (unsigned char byte : value) {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string HexDigits(std::uint64_t value, int digits) {
            static constexpr char kDigits[] = "0123456789abcdef";
            std::string out(static_cast<std::size_t>(digits), '0');
            for (int index = digits - 1; index >= 0; --index) {
                out[static_cast<std::size_t>(index)] = kDigits[value & 0xF];
                value >>= 4;
            }
            return out;
        }

        std::string Stem(const std::string &path) {
            const std::size_t slash = path.find_last_of('/');
            const std::string name  = slash == std::string::npos ? path : path.substr(slash + 1);
            const std::size_t dot   = name.find_last_of('.');
            return dot == std::string::npos ? name : name.substr(0, dot);
        }

        std::string EscapeXml(const std::string &value) {
            std::string out;
            out.reserve(value.size());
            for (char character : value) {
                switch (character) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&apos;"; break;
                default: out.push_back(character); break;
                }
            }
            return out;
        }

        bool ReadResourceFile(const std::string &resourceName, const std::string &path, std::string &contents) {
            const std::string virtualPath = Framework::Utils::Vfs::ResourcePath(resourceName) + "/" + path;
            return Framework::Utils::Vfs::Get().Read(virtualPath, contents);
        }

        struct Entry {
            std::string path;
            const std::string *data;
        };

        bool WriteArchive(const std::string &archivePath, const std::vector<Entry> &entries, std::string &error) {
            mz_zip_archive zip {};
            if (!mz_zip_writer_init_heap(&zip, 0, 128 * 1024)) {
                error = "could not start the archive writer";
                return false;
            }
            for (const Entry &entry : entries) {
                // Stored, like the bundled host archive CryPak already accepts.
                if (!mz_zip_writer_add_mem(&zip, entry.path.c_str(), entry.data->data(), entry.data->size(), MZ_NO_COMPRESSION)) {
                    mz_zip_writer_end(&zip);
                    error = "could not add " + entry.path + " to the archive";
                    return false;
                }
            }
            void *buffer     = nullptr;
            std::size_t size = 0;
            if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) {
                mz_zip_writer_end(&zip);
                error = "could not finalize the archive";
                return false;
            }
            std::error_code code;
            std::filesystem::create_directories(std::filesystem::path(archivePath).parent_path(), code);
            std::ofstream file(archivePath, std::ios::binary | std::ios::trunc);
            const bool written = file.is_open() && file.write(static_cast<const char *>(buffer), static_cast<std::streamsize>(size)).good();
            file.close();
            mz_zip_writer_end(&zip); // frees the heap buffer
            if (!written) {
                error = "could not write " + archivePath;
                return false;
            }
            return true;
        }
    } // namespace

    const std::vector<NativeUIAssets::Library> &NativeUIAssets::Libraries() {
        static const std::vector<Library> libraries = [] {
            std::vector<Library> parsed;
            const std::string &directory = ModuleDirectory();
            if (directory.empty()) {
                return parsed;
            }
            const std::string path = directory + "\\files\\kcdc\\native_ui\\inventory.json";
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) {
                return parsed;
            }
            nlohmann::json document;
            try {
                file >> document;
            }
            catch (const std::exception &) {
                return parsed;
            }
            const auto entries = document.find("native_libraries");
            if (entries == document.end() || !entries->is_array()) {
                return parsed;
            }
            for (const nlohmann::json &entry : *entries) {
                Library library;
                library.host  = entry.value("host", std::string {});
                library.movie = entry.value("movie", std::string {});
                if (library.host.empty()) {
                    continue;
                }
                // The generator names the default host without a suffix and every other after its own movie.
                constexpr char prefix[] = "KCDCNativeHost_";
                library.name            = library.host.rfind(prefix, 0) == 0 ? library.host.substr(sizeof(prefix) - 1) : std::string {};
                const auto exports      = entry.find("exports");
                if (exports != entry.end() && exports->is_array()) {
                    for (const nlohmann::json &name : *exports) {
                        if (name.is_string()) {
                            library.exports.push_back(name.get<std::string>());
                        }
                    }
                }
                parsed.push_back(std::move(library));
            }
            return parsed;
        }();
        return libraries;
    }

    const std::string &NativeUIAssets::ModuleDirectory() {
        static const std::string directory = [] {
            HMODULE module = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&NativeUIAssets::BundledArchive), &module)) {
                return std::string {};
            }
            char path[MAX_PATH] = {};
            const DWORD length  = GetModuleFileNameA(module, path, MAX_PATH);
            if (length == 0 || length >= MAX_PATH) {
                return std::string {};
            }
            const std::string full(path, length);
            const auto separator = full.find_last_of("\\/");
            return separator == std::string::npos ? std::string {} : full.substr(0, separator);
        }();
        return directory;
    }

    std::string NativeUIAssets::BundledArchive() {
        const std::string &directory = ModuleDirectory();
        return directory.empty() ? std::string {} : directory + "\\files\\kcdc\\native_ui\\native-ui-host.pak";
    }

    bool NativeUIAssets::BundledHost(const std::string &library, Movie &out, std::string &error) {
        out                       = {};
        const std::string archive = BundledArchive();
        if (archive.empty()) {
            error = "could not resolve the client module directory";
            return false;
        }
        if (!std::filesystem::exists(archive)) {
            error = "native-ui-host.pak is missing; run tools/build_native_ui_host.py --all-libraries";
            return false;
        }
        const std::string sanitized = Sanitize(library, 64);
        if (!library.empty() && sanitized != library) {
            error = "a native UI library name is an identifier: " + library;
            return false;
        }
        out.elementName    = library.empty() ? "KCDCNativeHost" : "KCDCNativeHost_" + library;
        out.archivePath    = archive;
        out.descriptorPath = "Libs/UI/UIElements/" + out.elementName + ".xml";
        return true;
    }

    bool NativeUIAssets::IsSafeRelativePath(const std::string &path) {
        if (path.empty() || path.size() > 200) {
            return false;
        }
        if (path.front() == '/' || path.front() == '\\' || path.find(':') != std::string::npos) {
            return false;
        }
        std::size_t segmentStart = 0;
        for (std::size_t index = 0; index <= path.size(); ++index) {
            const bool end = index == path.size();
            if (!end) {
                const char character = path[index];
                const bool allowed   = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-' || character == '/';
                if (!allowed) {
                    return false;
                }
                if (character != '/') {
                    continue;
                }
            }
            const std::string segment = path.substr(segmentStart, index - segmentStart);
            if (segment.empty() || segment == "." || segment == "..") {
                return false;
            }
            segmentStart = index + 1;
        }
        return true;
    }

    bool NativeUIAssets::PackageResourceMovie(const std::string &resourceName, const std::string &moviePath, const std::vector<std::string> &assetPaths, int layer, Movie &out, std::string &error) {
        out = {};
        if (resourceName.empty() || Sanitize(resourceName, 64) != resourceName) {
            error = "a resource name must be an identifier to host a native movie";
            return false;
        }
        if (!IsSafeRelativePath(moviePath)) {
            error = "the movie path must be a plain relative path inside the resource";
            return false;
        }
        if (assetPaths.size() > kMaxAssets) {
            error = "a native movie may ship at most " + std::to_string(kMaxAssets) + " extra files";
            return false;
        }
        if (layer < kMinLayer || layer > kMaxLayer) {
            error = "layer must be between " + std::to_string(kMinLayer) + " and " + std::to_string(kMaxLayer);
            return false;
        }
        const std::string &directory = ModuleDirectory();
        if (directory.empty()) {
            error = "could not resolve the client module directory";
            return false;
        }

        std::string movie;
        if (!ReadResourceFile(resourceName, moviePath, movie) || movie.empty()) {
            error = "the resource does not ship " + moviePath;
            return false;
        }
        if (movie.size() > kMaxMovieSize) {
            error = "a native movie may be at most 32 MiB";
            return false;
        }

        std::vector<std::string> assets;
        assets.reserve(assetPaths.size());
        std::uint64_t hash = Hash(movie, 14695981039346656037ull);
        for (const std::string &assetPath : assetPaths) {
            if (!IsSafeRelativePath(assetPath)) {
                error = "an asset path must be a plain relative path inside the resource: " + assetPath;
                return false;
            }
            std::string contents;
            if (!ReadResourceFile(resourceName, assetPath, contents)) {
                error = "the resource does not ship " + assetPath;
                return false;
            }
            hash = Hash(assetPath, hash);
            hash = Hash(contents, hash);
            assets.emplace_back(std::move(contents));
        }

        // The content hash is in the element name, so changed content registers a new element.
        const std::string stem    = Sanitize(Stem(moviePath), 48);
        const std::string version = HexDigits(hash, 8);
        out.elementName           = "KCDC_" + Sanitize(resourceName, 48) + "_" + (stem.empty() ? "movie" : stem) + "_" + version;
        const std::string folder  = "Libs/UI/kcdc/" + resourceName;
        const std::string base    = folder + "/" + (stem.empty() ? std::string("movie") : stem) + "_" + version;
        out.descriptorPath        = "Libs/UI/UIElements/" + out.elementName + ".xml";
        out.archivePath           = directory + "\\files\\kcdc\\native_ui\\cache\\" + resourceName + "-" + version + ".pak";

        std::string descriptor;
        descriptor += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        descriptor += "<UIElements name=\"KCDC\">\n";
        descriptor += "  <UIElement name=\"" + EscapeXml(out.elementName) + "\" cursor=\"0\" mouseevents=\"0\" keyevents=\"0\" controller_input=\"0\" events_exclusive=\"0\">\n";
        descriptor += "    <GFx file=\"" + EscapeXml(base.substr(std::string("Libs/UI/").size())) + ".gfx\" layer=\"" + std::to_string(layer) + "\" alpha=\"0\">\n";
        descriptor += "      <Constraints><Align mode=\"fullscreen\" scale=\"1\"/></Constraints>\n";
        descriptor += "    </GFx>\n";
        descriptor += "    <functions/>\n";
        descriptor += "    <events/>\n";
        descriptor += "  </UIElement>\n";
        descriptor += "</UIElements>\n";

        std::vector<Entry> entries;
        entries.push_back({base + ".gfx", &movie});
        entries.push_back({base + ".swf", &movie});
        entries.push_back({out.descriptorPath, &descriptor});
        for (std::size_t index = 0; index < assets.size(); ++index) {
            entries.push_back({folder + "/" + assetPaths[index], &assets[index]});
        }

        if (std::filesystem::exists(out.archivePath)) {
            return true; // Same hash, same content.
        }
        return WriteArchive(out.archivePath, entries, error);
    }

    bool NativeUIAssets::PackageBookImage(const std::string &resourceName, const std::string &imagePath, std::string &source, std::string &archivePath, std::string &error) {
        source.clear();
        archivePath.clear();
        if (resourceName.empty() || Sanitize(resourceName, 64) != resourceName) {
            error = "a resource name must be an identifier to ship a book image";
            return false;
        }
        if (!IsSafeRelativePath(imagePath)) {
            error = "the image path must be a plain relative path inside the resource";
            return false;
        }
        // The movie appends `.dds` itself, so the image loader only ever
        // takes the DDS route for a book picture.
        constexpr std::string_view kSuffix = ".dds";
        if (imagePath.size() <= kSuffix.size() || _stricmp(imagePath.c_str() + imagePath.size() - kSuffix.size(), ".dds") != 0) {
            error = "a book image must be a .dds file";
            return false;
        }
        const std::string &directory = ModuleDirectory();
        if (directory.empty()) {
            error = "could not resolve the client module directory";
            return false;
        }

        std::string contents;
        if (!ReadResourceFile(resourceName, imagePath, contents) || contents.empty()) {
            error = "the resource does not ship " + imagePath;
            return false;
        }
        if (contents.size() > kMaxBookImageSize) {
            error = "a book image may be at most 16 MiB";
            return false;
        }

        const std::string version = HexDigits(Hash(contents, Hash(imagePath, 14695981039346656037ull)), 8);
        const std::string stem    = imagePath.substr(0, imagePath.size() - kSuffix.size());
        source                    = "kcdc/" + resourceName + "/" + version + "/" + stem;
        archivePath               = directory + "\\files\\kcdc\\native_ui\\cache\\" + resourceName + "-book-" + version + ".pak";

        const std::string entry = BookImagePath(source);
        if (std::filesystem::exists(archivePath)) {
            return true; // Same hash, same content.
        }
        return WriteArchive(archivePath, {{entry, &contents}}, error);
    }

    std::string NativeUIAssets::BookImagePath(const std::string &source) {
        if (source.rfind("kcdc/", 0) != 0 || !IsSafeRelativePath(source)) {
            return {};
        }
        // The prefix and suffixes GeneralBook.gfx puts around an `<img src>`.
        return "Libs/UI/Textures/Books/Unique/" + source + "_ui.dds";
    }
} // namespace KCDC::Core
