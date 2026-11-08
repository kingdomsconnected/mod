#pragma once

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace KCDC::Shared::WorldBuilder {
    // MCP may use the maps directory and the export directory already chosen
    // by the user. An MCP request cannot add a new trusted root.
    class McpFilePolicy final {
      public:
        static std::string Utf8(const std::filesystem::path &path) {
            const auto text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        static std::filesystem::path Canonical(const std::filesystem::path &path) {
            std::error_code error;
            const auto absolute = std::filesystem::absolute(path, error);
            if (error) {
                throw std::runtime_error("Cannot resolve absolute file path");
            }
            const auto resolved = std::filesystem::weakly_canonical(absolute, error);
            if (error || resolved.empty()) {
                throw std::runtime_error("Cannot resolve file path");
            }
            return resolved;
        }

        static std::string Resolve(const std::string &input, const std::string &mapsRoot, const std::string &exportRoot, bool writing = false) {
            if (input.empty() || input.size() > 1000 || input.find('\0') != std::string::npos) {
                throw std::runtime_error("An explicit local path of at most 1000 bytes is required");
            }
            const auto parsed = std::filesystem::u8path(input);
            if (input.rfind("\\\\", 0) == 0 || input.rfind("//", 0) == 0 || (parsed.has_root_name() && !parsed.is_absolute())) {
                throw std::runtime_error("Network, device and drive-relative paths are not allowed");
            }
            for (const auto &part : parsed.relative_path()) {
                const auto component = part.native();
                if (component.find_first_of(L":*?\"<>|") != std::wstring::npos
                    || std::any_of(component.begin(), component.end(),
                        [](wchar_t c) {
                            return c < 32;
                        })
                    || (!component.empty() && component != L"." && component != L".." && (component.back() == L'.' || component.back() == L' '))) {
                    throw std::runtime_error("Invalid path component");
                }
                auto stem = component.substr(0, component.find(L'.'));
                std::transform(stem.begin(), stem.end(), stem.begin(), [](wchar_t c) {
                    return static_cast<wchar_t>(std::towupper(c));
                });
                if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" || stem == L"CONIN$" || stem == L"CONOUT$" || (stem.size() == 4 && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT") && stem[3] >= L'1' && stem[3] <= L'9'))
                    throw std::runtime_error("Windows device names are not allowed");
            }
            if (mapsRoot.empty()) {
                throw std::runtime_error("The maps directory is unavailable");
            }
            const auto maps     = Canonical(std::filesystem::u8path(mapsRoot));
            const auto resolved = Canonical(parsed.is_absolute() ? parsed : maps / parsed);
            if (!Inside(resolved, maps) && (exportRoot.empty() || !Inside(resolved, Canonical(std::filesystem::u8path(exportRoot))))) {
                throw std::runtime_error("Path is outside maps and the export directory selected in World Builder");
            }
            // Follow symlinks/junctions before the boundary check; reject other
            // reparse points rather than guessing their redirection semantics.
            std::filesystem::path prefix;
            for (const auto &part : resolved) {
                prefix /= part;
                const DWORD attributes = GetFileAttributesW(prefix.c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                    throw std::runtime_error("Reparse points cannot be used by MCP file operations");
                }
            }
            if (writing) {
                auto staging = resolved;
                staging += L".tmp";
                if (GetFileAttributesW(staging.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    throw std::runtime_error("A staged file already exists beside this destination; inspect it before retrying");
                }
            }
            return Utf8(resolved);
        }

      private:
        static bool Inside(const std::filesystem::path &path, const std::filesystem::path &root) {
            auto candidate = path.begin();
            for (auto base = root.begin(); base != root.end(); ++base, ++candidate) {
                if (candidate == path.end() || CompareStringOrdinal(candidate->c_str(), -1, base->c_str(), -1, TRUE) != CSTR_EQUAL) {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace KCDC::Shared::WorldBuilder
