#include "soul_catalog.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>

#include <fstream>
#include <string>


namespace KCDC::Common::Spawn {
    namespace {
        /**
         * Directory holding the client DLL. The catalog ships next to it, not
         * next to the game executable the framework's path helper resolves
         * against.
         */
        std::string ModuleDirectory() {
            HMODULE module = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(&ModuleDirectory), &module)) {
                return {};
            }

            char path[MAX_PATH] = {};
            const DWORD length  = GetModuleFileNameA(module, path, MAX_PATH);
            if (length == 0 || length >= MAX_PATH) {
                return {};
            }

            std::string full(path, length);
            const auto separator = full.find_last_of("\\/");
            return separator == std::string::npos ? std::string {} : full.substr(0, separator);
        }

        std::string TrimCarriageReturn(std::string value) {
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
                value.pop_back();
            }
            return value;
        }
    } // namespace

    bool SoulCatalog::Load() {
        _souls.clear();
        _error.clear();
        _loaded = false;

        const std::string directory = ModuleDirectory();
        if (directory.empty()) {
            _error = "could not resolve the client module directory";
            return false;
        }

        _path = directory + "\\files\\kcdc\\souls.tsv";

        std::ifstream file(_path);
        if (!file.is_open()) {
            _error = "not found; run tools/generate_soul_catalog.py";
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            line = TrimCarriageReturn(std::move(line));
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // name<TAB>archetype<TAB>soul_id<TAB>character
            std::string fields[4];
            std::size_t field = 0;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= line.size() && field < 4; ++i) {
                if (i == line.size() || line[i] == '\t') {
                    fields[field++] = line.substr(start, i - start);
                    start           = i + 1;
                }
            }

            if (fields[0].empty()) {
                continue;
            }

            SoulEntry entry;
            entry.name      = std::move(fields[0]);
            entry.archetype = std::move(fields[1]);
            entry.guid      = std::move(fields[2]);
            entry.character = std::move(fields[3]);
            _souls.push_back(std::move(entry));
        }

        _loaded = true;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Loaded {} souls from {}", _souls.size(), _path);
        return true;
    }
} // namespace KCDC::Common::Spawn
