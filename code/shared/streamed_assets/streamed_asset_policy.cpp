#include "streamed_asset_policy.h"

#include "lua_script_scan.h"

#include <array>
#include <cctype>
#include <fstream>
#include <iterator>

namespace KCDC::Shared::StreamedAssets {
    namespace {
        // The kinds the engine loads by path and nothing else: no script, no table, no shader.
        constexpr std::array<std::string_view, 15> kAssetExtensions = {".cgf", ".cgfm", ".chr", ".chrm", ".skin", ".skinm", ".cga", ".cdf", ".mtl", ".dds", ".dds.a", ".caf", ".adb", ".animevents", ".ogg"};

        constexpr std::array<std::string_view, 4> kBankExtensions = {".bank", ".strings.bank", ".assets.bank", ".streams.bank"};

        // Top-level data folders a namespaced stream-lane entry may live under.
        constexpr std::array<std::string_view, 6> kStreamCategories = {"objects", "characters", "textures", "animations", "sounds", "materials"};

        // Stock folders that hold code or data rather than assets. A replacement there would be a
        // server rewriting the game's logic, not its look.
        constexpr std::array<std::string_view, 7> kReplaceDeniedRoots = {"scripts", "libs", "engine", "shaders", "levels", "localization", "editor"};

        // Where the database looks for tables and the patches a mod merges into them.
        constexpr std::array<std::string_view, 2> kTableFolder = {"libs", "tables"};

        // The engine folders whose roots are fixed, so their entries are namespaced by file prefix.
        constexpr std::array<std::string_view, 4> kFmodBankFolder        = {"sounds", "fmod", "build", "pc"};
        constexpr std::array<std::string_view, 2> kParticleLibraryFolder = {"libs", "particles"};

        // What a level edit may replace inside `levels/<level>/`. Level metadata stays closed: a
        // level's info file names a script the load runs.
        constexpr std::array<std::string_view, 7> kLevelFolders    = {"terrain", "physics", "recast", "hlods", "cestool", "brush", "layers"};
        constexpr std::array<std::string_view, 8> kLevelExtensions = {".dat", ".lst", ".ctc", ".xml", ".cgf", ".cgfm", ".hdr", ".nav"};
        constexpr std::array<std::string_view, 4> kLevelStoredExtensions = {".dat", ".ctc", ".hdr", ".nav"};

        template <std::size_t N>
        bool Contains(const std::array<std::string_view, N> &set, std::string_view value) {
            for (const std::string_view entry : set) {
                if (entry == value) {
                    return true;
                }
            }
            return false;
        }

        bool IsDigits(std::string_view text) {
            if (text.empty() || text.size() > 2) {
                return false;
            }
            for (const char c : text) {
                if (c < '0' || c > '9') {
                    return false;
                }
            }
            return text != "0" && text != "00";
        }

        // Split DDS chunks: `.dds.1`..`.dds.99` and the alpha chunks `.dds.1a`..`.dds.99a`.
        bool IsSplitTextureChunk(std::string_view extension) {
            constexpr std::string_view prefix = ".dds.";
            if (extension.substr(0, prefix.size()) != prefix) {
                return false;
            }
            std::string_view index = extension.substr(prefix.size());
            if (!index.empty() && index.back() == 'a') {
                index.remove_suffix(1);
            }
            return IsDigits(index);
        }

        bool IsAssetExtension(std::string_view extension) {
            return Contains(kAssetExtensions, extension) || IsSplitTextureChunk(extension);
        }

        std::vector<std::string_view> Split(std::string_view path) {
            std::vector<std::string_view> segments;
            std::size_t start = 0;
            while (start <= path.size()) {
                const std::size_t slash = path.find('/', start);
                const std::size_t end   = slash == std::string_view::npos ? path.size() : slash;
                segments.push_back(path.substr(start, end - start));
                if (slash == std::string_view::npos) {
                    break;
                }
                start = slash + 1;
            }
            return segments;
        }

        std::string_view ExtensionOf(std::string_view file) {
            const std::size_t dot = file.find('.');
            return dot == std::string_view::npos ? std::string_view {} : file.substr(dot);
        }

        bool EndsWith(std::string_view text, std::string_view suffix) {
            return text.size() > suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        // Inside `folder`, at any depth below it.
        template <std::size_t N>
        bool UnderFolder(const std::vector<std::string_view> &segments, const std::array<std::string_view, N> &folder) {
            if (segments.size() <= N) {
                return false;
            }
            for (std::size_t i = 0; i < N; ++i) {
                if (segments[i] != folder[i]) {
                    return false;
                }
            }
            return true;
        }

        // Directly inside `folder`.
        template <std::size_t N>
        bool InFolder(const std::vector<std::string_view> &segments, const std::array<std::string_view, N> &folder) {
            return segments.size() == N + 1 && UnderFolder(segments, folder);
        }

        // `<file>__<id>.xml`: the naming the game's own mod loader merges into `<file>.xml` rather
        // than loading in its place.
        bool IsPatchFile(std::string_view file) {
            constexpr std::string_view extension = ".xml";
            if (!EndsWith(file, extension)) {
                return false;
            }
            const std::string_view stem = file.substr(0, file.size() - extension.size());
            const std::size_t marker    = stem.rfind("__");
            if (marker == std::string_view::npos || marker == 0 || marker + 2 >= stem.size()) {
                return false;
            }
            for (const char c : stem.substr(marker + 2)) {
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
                    return false;
                }
            }
            return stem.find('.') == std::string_view::npos;
        }

        bool StartsWith(std::string_view text, std::string_view prefix) {
            return text.size() > prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        // `whdata_<n>`, the level's own brush data, has no extension.
        bool IsLevelWhData(std::string_view file) {
            constexpr std::string_view prefix = "whdata_";
            if (!StartsWith(file, prefix)) {
                return false;
            }
            for (const char c : file.substr(prefix.size())) {
                if (c < '0' || c > '9') {
                    return false;
                }
            }
            return true;
        }

        bool IsLevelEntityFile(std::string_view file) {
            return EndsWith(file, ".xml") && (StartsWith(file, "objects_") || StartsWith(file, "mission_"));
        }

        // Whether a normalised path is one a level edit may replace.
        bool IsLevelEntry(const std::vector<std::string_view> &segments) {
            if (segments.size() < 3 || segments[0] != "levels") {
                return false;
            }
            for (const char c : segments[1]) {
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
                    return false;
                }
            }
            const std::string_view file = segments.back();
            if (segments.size() == 3) {
                return file == "occluder.ocm" || IsLevelWhData(file) || IsLevelEntityFile(file);
            }
            if (segments.size() == 5 && segments[2] == "tables" && segments[3] == "ai") {
                return file == "scheduler.xml";
            }
            return Contains(kLevelFolders, segments[2]) && Contains(kLevelExtensions, ExtensionOf(file));
        }

        // `kcdc_<resource>_<name>` with a name of its own.
        bool HasResourcePrefix(std::string_view file, std::string_view resource) {
            const std::string prefix = std::string(kNamespaceSegment) + "_" + std::string(resource) + "_";
            const std::string_view stem = file.substr(0, file.find('.'));
            return stem.size() > prefix.size() && stem.substr(0, prefix.size()) == prefix;
        }
    } // namespace

    const std::shared_ptr<const Policy> &Policy::Get() {
        static const std::shared_ptr<const Policy> instance = std::make_shared<Policy>();
        return instance;
    }

    const std::vector<std::string> &Policy::GetLanes() const {
        static const std::vector<std::string> lanes = {kLaneStream, kLaneReplace, kLaneLocalization, kLaneLevel, kLaneScript};
        return lanes;
    }

    bool Policy::NormalizeEntry(std::string_view lane, std::string_view resource, std::string_view raw, std::string &out, std::string &reason) const {
        if (raw.empty() || raw.size() > Framework::Utils::StreamedAssets::kMaxPathLength) {
            reason = "path is empty or too long";
            return false;
        }

        std::string path;
        path.reserve(raw.size());
        for (const char c : raw) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (c == '\\') {
                path.push_back('/');
            }
            else if (std::isalnum(u) != 0 || c == '_' || c == '-' || c == '.' || c == '/') {
                path.push_back(static_cast<char>(std::tolower(u)));
            }
            else {
                reason = "path has a character outside [A-Za-z0-9_./-]";
                return false;
            }
        }

        const std::vector<std::string_view> segments = Split(path);
        if (segments.size() < 2) {
            reason = "path has no folder";
            return false;
        }
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const std::string_view segment = segments[i];
            if (segment.empty() || segment == "." || segment == "..") {
                reason = "path has an empty or relative segment";
                return false;
            }
            if (i + 1 < segments.size() && segment.find('.') != std::string_view::npos) {
                reason = "a folder name contains a dot";
                return false;
            }
        }

        const std::string_view file      = segments.back();
        const std::string_view extension = ExtensionOf(file);
        if ((extension.empty() && lane != kLaneLevel) || file.front() == '.') {
            reason = "file has no name or no extension";
            return false;
        }

        if (lane == kLaneStream) {
            if (InFolder(segments, kFmodBankFolder)) {
                if (!Contains(kBankExtensions, extension) || !HasResourcePrefix(file, resource)) {
                    reason = "FMOD banks must be named kcdc_<resource>_<name>.bank";
                    return false;
                }
            }
            else if (InFolder(segments, kParticleLibraryFolder)) {
                if (extension != ".xml" || !HasResourcePrefix(file, resource)) {
                    reason = "particle libraries must be named kcdc_<resource>_<name>.xml";
                    return false;
                }
            }
            else {
                if (segments.size() < 4 || !Contains(kStreamCategories, segments[0]) || segments[1] != kNamespaceSegment || segments[2] != resource) {
                    reason = "stream entries must sit at <category>/kcdc/<resource>/...";
                    return false;
                }
                const bool audioControls = extension == ".xml" && segments.size() == 5 && segments[0] == "sounds" && segments[3] == "atl";
                if (!audioControls && !IsAssetExtension(extension)) {
                    reason = "extension is not an asset kind the engine loads by path";
                    return false;
                }
            }
        }
        else if (lane == kLaneReplace && UnderFolder(segments, kTableFolder)) {
            // Tables are game logic, so only the row patches a mod merges in, never a whole table.
            if (!IsTablePatch(path)) {
                reason = "tables can only be patched, as libs/tables/.../<table>__<id>.xml";
                return false;
            }
        }
        else if (lane == kLaneLocalization) {
            if (segments.size() != 2 || !IsPatchFile(file)) {
                reason = "localization entries must be <language>/<file>__<id>.xml";
                return false;
            }
            for (const char c : segments[0]) {
                if (c < 'a' || c > 'z') {
                    reason = "a language folder is lowercase letters";
                    return false;
                }
            }
        }
        else if (lane == kLaneReplace) {
            if (!IsAssetExtension(extension)) {
                reason = "extension is not an asset kind the engine loads by path";
                return false;
            }
            if (Contains(kReplaceDeniedRoots, segments[0])) {
                reason = "replacements may not touch this folder";
                return false;
            }
            if (segments[1] == kNamespaceSegment) {
                reason = "the kcdc namespace belongs to the stream lane";
                return false;
            }
            // The animation image keeps every stock clip's metadata, so a replaced CAF plays
            // against a stale duration and controller count.
            if (extension == ".caf") {
                reason = "stock animations cannot be replaced globally";
                return false;
            }
        }
        else if (lane == kLaneLevel) {
            if (!IsLevelEntry(segments)) {
                reason = "not a file a level edit may replace";
                return false;
            }
        }
        else if (lane == kLaneScript) {
            if (segments.size() < 3 || segments[0] != "scripts" || extension != ".lua") {
                reason = "scripts must be scripts/.../<name>.lua";
                return false;
            }
        }
        else {
            reason = "unknown lane";
            return false;
        }

        out = std::move(path);
        return true;
    }

    bool Policy::PrefersStore(std::string_view normalized) const {
        const std::size_t slash          = normalized.rfind('/');
        const std::string_view extension = ExtensionOf(normalized.substr(slash == std::string_view::npos ? 0 : slash + 1));
        // A level's terrain, physics and navigation are read in pieces by offset and the game's own
        // paks store them: deflated, a level streams in minutes rather than seconds.
        if (normalized.substr(0, 7) == "levels/" && Contains(kLevelStoredExtensions, extension)) {
            return true;
        }
        // FMOD reads banks in place and streams their sample data; DDS mips and ogg are streamed
        // or already compressed.
        return extension == ".ogg" || extension.substr(0, 4) == ".dds" || Contains(kBankExtensions, extension);
    }

    bool Policy::ShouldShip(std::string_view lane, std::string_view, std::string_view normalized, const std::string &sourceFile, std::string &reason) const {
        const bool script = lane == kLaneScript;
        if (!script && (lane != kLaneLevel || !IsLevelEntityXml(normalized))) {
            return true;
        }
        std::ifstream file(sourceFile, std::ios::binary);
        const std::string text {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (!file.good() && !file.eof()) {
            reason = "the file could not be read";
            return false;
        }
        if (script && ScanLuaScript(text).unreadable) {
            reason = "the script is precompiled";
            return false;
        }
        if (!script && LevelXmlRunsScript(text)) {
            reason = "the level's entity XML runs a script";
            return false;
        }
        return true;
    }

    bool Policy::IsLevelEntityXml(std::string_view normalized) {
        const std::vector<std::string_view> segments = Split(normalized);
        if (segments.size() < 3 || segments[0] != "levels") {
            return false;
        }
        return (segments.size() == 3 && IsLevelEntityFile(segments.back())) || (segments.size() > 3 && segments[2] == "layers" && EndsWith(normalized, ".xml"));
    }

    bool Policy::LevelXmlRunsScript(std::string_view xml) {
        std::string folded(xml);
        for (char &c : folded) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        constexpr std::string_view command = "scriptcommand";
        constexpr std::string_view empty   = "=\"\"";
        for (std::size_t at = folded.find(command); at != std::string::npos; at = folded.find(command, at + command.size())) {
            if (folded.compare(at + command.size(), empty.size(), empty) != 0) {
                return true;
            }
        }

        // A flow node names its class in a value, where a character reference would hide the name
        // from the search above. The game's own classes never use one.
        constexpr std::string_view classAttribute = "class=\"";
        for (std::size_t at = folded.find(classAttribute); at != std::string::npos; at = folded.find(classAttribute, at + classAttribute.size())) {
            const std::size_t value = at + classAttribute.size();
            const std::size_t close = folded.find('"', value);
            if (close == std::string::npos || folded.find('&', value) < close) {
                return true;
            }
        }
        return false;
    }

    bool Policy::IsModEntryScript(std::string_view normalized) {
        const std::vector<std::string_view> segments = Split(normalized);
        return segments.size() == 3 && segments[0] == "scripts" && segments[1] == "mods" && EndsWith(normalized, ".lua");
    }

    bool Policy::IsAnimationClip(std::string_view normalized) {
        return EndsWith(normalized, ".caf");
    }

    bool Policy::IsAnimationDatabase(std::string_view normalized) {
        return EndsWith(normalized, ".adb");
    }

    bool Policy::IsTextureBase(std::string_view normalized) {
        return EndsWith(normalized, ".dds");
    }

    bool Policy::IsMaterial(std::string_view normalized) {
        return EndsWith(normalized, ".mtl");
    }

    bool Policy::IsStaticMesh(std::string_view normalized) {
        return EndsWith(normalized, ".cgf");
    }

    bool Policy::IsAudioControls(std::string_view normalized) {
        const std::vector<std::string_view> segments = Split(normalized);
        return segments.size() == 5 && segments[0] == "sounds" && segments[1] == kNamespaceSegment && segments[3] == "atl" && EndsWith(normalized, ".xml");
    }

    bool Policy::IsFmodBank(std::string_view normalized) {
        const std::vector<std::string_view> segments = Split(normalized);
        return InFolder(segments, kFmodBankFolder) && Contains(kBankExtensions, ExtensionOf(segments.back()));
    }

    bool Policy::IsParticleLibrary(std::string_view normalized) {
        const std::vector<std::string_view> segments = Split(normalized);
        return InFolder(segments, kParticleLibraryFolder) && EndsWith(normalized, ".xml");
    }

    bool Policy::IsTablePatch(std::string_view normalized) {
        const std::vector<std::string_view> segments = Split(normalized);
        return UnderFolder(segments, kTableFolder) && IsPatchFile(segments.back());
    }

    std::string_view Policy::LanguageOf(std::string_view normalized) {
        const std::size_t slash = normalized.find('/');
        return slash == std::string_view::npos ? std::string_view {} : normalized.substr(0, slash);
    }

    std::string Policy::PatchedFileOf(std::string_view normalized) {
        const std::size_t slash     = normalized.rfind('/');
        const std::string_view file = normalized.substr(slash == std::string_view::npos ? 0 : slash + 1);
        return std::string(file.substr(0, file.rfind("__"))) + ".xml";
    }

    bool Policy::IsStreamedModelPath(std::string_view path) {
        return IsStreamedPath(path, "objects", ".cgf");
    }

    bool Policy::IsStreamedClipPath(std::string_view path) {
        return IsStreamedPath(path, "animations", ".caf");
    }

    bool Policy::IsStreamedEffectName(std::string_view name) {
        constexpr std::size_t kMaxEffectName = 128;
        const std::size_t dot                = name.find('.');
        if (name.empty() || name.size() > kMaxEffectName || dot == std::string_view::npos || dot + 1 >= name.size()) {
            return false;
        }
        for (const char c : name) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
                return false;
            }
        }
        const std::string_view library = name.substr(0, dot);
        const std::string prefix       = std::string(kNamespaceSegment) + "_";
        for (const char c : library) {
            if (c >= 'A' && c <= 'Z') {
                return false;
            }
        }
        return library.size() > prefix.size() && library.substr(0, prefix.size()) == prefix && name.find("..") == std::string_view::npos;
    }

    std::string Policy::ParticleLibraryEntryOf(std::string_view effectName) {
        return "libs/particles/" + std::string(effectName.substr(0, effectName.find('.'))) + ".xml";
    }

    bool Policy::IsStreamedPath(std::string_view path, std::string_view category, std::string_view extension) {
        const std::vector<std::string_view> segments = Split(path);
        if (segments.size() < 4 || segments[0] != category || segments[1] != kNamespaceSegment || !Framework::Utils::StreamedAssets::IsValidResourceName(segments[2]) || !EndsWith(path, extension)) {
            return false;
        }
        for (std::size_t i = 0; i < segments.size(); ++i) {
            if (segments[i].empty() || segments[i] == "." || segments[i] == ".." || (i + 1 < segments.size() && segments[i].find('.') != std::string_view::npos)) {
                return false;
            }
        }
        for (const char c : path) {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) {
                return false;
            }
        }
        return path.size() <= Framework::Utils::StreamedAssets::kMaxPathLength;
    }

    std::string Policy::ResourceOf(std::string_view normalized) {
        const std::vector<std::string_view> segments = Split(normalized);
        if (segments.size() >= 4 && segments[1] == kNamespaceSegment) {
            return std::string(segments[2]);
        }
        return {};
    }
} // namespace KCDC::Shared::StreamedAssets
