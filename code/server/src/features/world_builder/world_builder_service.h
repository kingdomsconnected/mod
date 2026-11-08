#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::WorldBuilder {
    /** One loaded `.world.json` export and the network ids of everything it spawned. */
    struct WorldResourceRecord {
        std::uint32_t handle = 0;
        std::string name;
        std::string path; // canonical, UTF-8
        std::string level;
        std::vector<std::uint64_t> props;
        std::vector<std::uint64_t> effects;
        std::vector<std::uint64_t> lights;
        std::vector<std::uint64_t> levelEdits;
        std::vector<std::uint64_t> doors;
        std::vector<std::uint64_t> smartObjects;
        std::vector<std::uint64_t> items;
        std::vector<std::uint32_t> areas;
        std::vector<std::uint32_t> patrolRoutes;
    };

    /**
     * The exports `mod.world_resources` names, loaded once at startup. The
     * runtime APIs (`Prop.spawn`, `Vfx.spawn`, `LevelEdit.apply`,
     * `Area.create`) spawn one thing each; bulk content arrives only here.
     */
    class WorldBuilderService final {
      public:
        /** Validates every export before spawning any, then spawns them all or none. False with `error` set on the first failure. */
        bool Load(const nlohmann::json &entries, const std::string &level, std::string &error);

        /** Destroys everything the exports spawned that still stands. */
        void Reset();

        /** True once every configured export has loaded, including an empty list. */
        bool IsReady() const {
            return _ready;
        }

        const std::vector<WorldResourceRecord> &All() const {
            return _resources;
        }

        /** The export with this exact name, or null. */
        const WorldResourceRecord *Find(const std::string &name) const;

        /** The export behind a scripting handle, or null once it is gone. */
        const WorldResourceRecord *Get(std::uint32_t handle) const;

      private:
        /** Spawns one validated export into `resource`; false with `error` set on the first entry that fails. */
        static bool Spawn(const nlohmann::json &document, WorldResourceRecord &resource, std::string &error);
        static void DestroyContents(const std::vector<WorldResourceRecord> &resources);

        std::vector<WorldResourceRecord> _resources;
        std::uint32_t _nextHandle = 1;
        bool _ready               = false;
    };
} // namespace KCDC::Features::WorldBuilder
