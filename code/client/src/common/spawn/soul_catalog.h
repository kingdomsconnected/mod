#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace KCDC::Common::Spawn {
    /** One row of the shipped soul catalog. */
    struct SoulEntry {
        std::string name;      // `soul_name`, for browsing
        std::string archetype; // `soul_archetype_name`: Horse, NPC, NPC_Female, Dog, ...
        std::string guid;      // `soul_id`, the value the spawn actually selects on
        std::string character; // `skald_character_name` when the soul names one
    };

    /**
     * Soul names a spawned actor can be given.
     *
     * A KCD2 actor takes its identity and appearance from a soul selected at
     * spawn time by its `soul_id` GUID. The game exposes a by-name soul lookup
     * but no enumerator, so the browsable list is generated offline by
     * `tools/generate_soul_catalog.py` and shipped as `files/kcdc/souls.tsv`
     * next to the client DLL. GUIDs typed by hand work without the catalog.
     */
    class SoulCatalog final {
      public:
        /** Reads the catalog from disk. Safe to call again to reload. */
        bool Load();

        bool IsLoaded() const {
            return _loaded;
        }

        const std::string &GetError() const {
            return _error;
        }

        const std::string &GetPath() const {
            return _path;
        }

        const std::vector<SoulEntry> &GetSouls() const {
            return _souls;
        }


      private:
        std::vector<SoulEntry> _souls;
        std::string _path;
        std::string _error;
        bool _loaded = false;
    };
} // namespace KCDC::Common::Spawn
