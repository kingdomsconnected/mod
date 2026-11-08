#pragma once

#include "features/area/area_service.h"
#include "features/inventory/inventory.h"
#include "shared/features/gathering/gathering_projection.h"
#include "shared/game/gathering_sources.h"
#include "shared/rate_limit.h"

#include <function2/function2.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace KCDC::Features::Gathering {
    /** `begin`, `complete` or `sync` from the owner. */
    struct GatheringRequest {
        std::string action;
        std::string id;
        std::uint8_t kind = 0;
        glm::vec3 position {0.0F};
        // A completion's sources: the same-kind plants the owner's game
        // would harvest with the picked one (QuerySources 0x182ADDA1C).
        std::vector<glm::vec3> sources;
        bool full                    = false;
        std::uint64_t syncGeneration = 0;
    };

    /** One picked source; until is world milliseconds, or zero for manual regrowth only. */
    struct GatheringPoint {
        std::uint8_t kind = 0;
        glm::vec3 position {0.0F};
        std::uint64_t until    = 0;
        std::uint64_t sequence = 0;
        bool removed           = false;
    };

    /** A completion's answer. `code` is empty for a granted harvest. */
    struct GatheringHarvest {
        std::string code;
        std::uint8_t kind    = 0;
        std::uint32_t amount = 0;
        glm::vec3 position {0.0F};
        std::vector<KCDC::Features::Inventory::InventoryUnit> outputs;
        std::vector<GatheringPoint> depleted;
    };

    struct GatheringHooks {
        fu2::function<KCDC::Features::Inventory::Inventory *(std::uint64_t playerId) const> inventory;
        // Synchronous veto: false refuses. A throw counts as a refusal.
        fu2::function<bool(std::uint64_t playerId, const nlohmann::json &proposal) const> harvesting;
        fu2::function<void(std::uint64_t playerId, double xp) const> experience;
        // Null before the player's first progression digest.
        fu2::function<std::optional<float>(std::uint64_t playerId) const> survivalLevel;
    };

    struct GatheringRespawnTime {
        double minHours = 0;
        double maxHours = 0;
    };

    /**
     * Herb picking, held in memory.
     *
     * The owner's game says which plant it picked and which same-kind plants
     * around it the game would harvest with it; the server takes that on
     * trust within reach of the player and within its own harvest radius for
     * the player's survival level, paces it, and remembers every counted plant
     * as picked until its configured regrowth time passes on the world clock,
     * or until a script respawns it when timed regrowth is disabled.
     * Nothing survives a restart: every plant regrows.
     */
    class GatheringModule final {
      public:
        static constexpr float kReach = 3.0F;
        // Two reports this close are one plant: authored same-kind plants
        // are at least 0.29 m apart in 99 % of cases.
        static constexpr float kMatchRadius = 0.1F;
        // The game counts every source in the radius; 32 is above the 99th
        // percentile of the authored vegetation at the widest radius.
        static constexpr std::uint32_t kMaximumSources    = 32;
        static constexpr std::uint64_t kMinimumMs         = 500;
        static constexpr std::uint64_t kMaximumMs         = 30000;
        static constexpr double kExperience               = 5.0;
        static constexpr double kDefaultRespawnMultiplier = 0.2;

        explicit GatheringModule(GatheringHooks hooks): _hooks(std::move(hooks)) {}

        void SetSources(const std::vector<KCDC::Shared::Game::GatheringSource> &sources);
        std::vector<KCDC::Shared::Game::GatheringSource> GetHerbs(const KCDC::Features::Area::AreaService &areas, const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld, std::uint64_t worldMs) const;
        std::vector<GatheringPoint> RemoveHerbs(const KCDC::Features::Area::AreaService &areas, const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld, std::uint64_t worldMs);
        std::vector<GatheringPoint> RespawnHerbs(const KCDC::Features::Area::AreaService &areas, const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld, std::uint64_t worldMs);

        static bool ReadRequest(const std::string &body, GatheringRequest &request);

        /** Empty when the attempt started, else the refusal. */
        std::string Begin(std::uint64_t playerId, const GatheringRequest &request, const glm::vec3 &player, std::uint32_t virtualWorld, std::uint64_t nowMs, std::uint64_t worldMs);
        GatheringHarvest Complete(std::uint64_t playerId, const GatheringRequest &completion, const glm::vec3 &player, std::uint32_t virtualWorld, bool canAct, std::uint64_t nowMs, std::uint64_t worldMs);

        bool IsDepleted(std::uint32_t virtualWorld, std::uint8_t kind, const glm::vec3 &position, std::uint64_t worldMs) const;
        /** The committed source to replay when refusing an already-picked plant. */
        const GatheringPoint *FindDepleted(std::uint32_t virtualWorld, std::uint8_t kind, const glm::vec3 &position, std::uint64_t worldMs, bool exactSource = false) const;
        bool SetRespawnTime(double minHours, double maxHours);
        void ResetRespawnTime() {
            _respawnTime.reset();
        }
        const std::optional<GatheringRespawnTime> &GetRespawnTime() const {
            return _respawnTime;
        }
        /** Restart every active deadline from now, grouped for dispatch to its world. */
        std::map<std::uint32_t, std::vector<GatheringPoint>> Reschedule(std::uint64_t worldMs);
        /** Remove active depletion in a 3D sphere, or throughout the world when position is empty. */
        std::vector<GatheringPoint> Respawn(std::uint32_t virtualWorld, const std::optional<glm::vec3> &position, float radius, std::uint64_t worldMs);
        /**
         * What a player at `position` has not been told yet: everything picked
         * in the cells around it after moving to another cell, only the new
         * picks otherwise. Null when there is nothing to send or the player
         * asks too often.
         */
        std::optional<std::vector<GatheringPoint>> Sync(std::uint64_t playerId, std::uint32_t virtualWorld, const glm::vec3 &position, std::uint64_t nowMs, std::uint64_t worldMs, bool full = false);
        /** Whether a player at `position` is told about a pick at `point`. */
        static bool Covers(const glm::vec3 &position, const glm::vec3 &point);
        /** Forgets plants that have regrown. */
        void Prune(std::uint64_t worldMs);
        /** Regrows every plant. */
        void Regrow();
        void Forget(std::uint64_t playerId);

      private:
        struct Attempt {
            std::string id;
            GatheringRequest request;
            std::uint32_t virtualWorld = 0;
            std::uint64_t startedMs    = 0;
        };
        struct Cell {
            std::uint32_t virtualWorld = 0;
            std::int32_t x             = 0;
            std::int32_t y             = 0;

            auto operator<=>(const Cell &) const = default;
        };
        struct Player {
            std::optional<Attempt> attempt;
            // A player picks a plant every few seconds at most.
            KCDC::Shared::RateLimit limit {1.0, 5.0};
            // And asks what is picked around them no more than that either.
            KCDC::Shared::RateLimit syncLimit {0.2, 3.0};
            std::optional<Cell> syncCell;
            std::uint64_t syncSequence = 0;
        };

        static Cell CellOf(std::uint32_t virtualWorld, const glm::vec3 &position);
        std::vector<const KCDC::Shared::Game::GatheringSource *> SourcesInArea(const KCDC::Features::Area::AreaService &areas, const KCDC::Features::Area::AreaRecord &area, std::uint32_t virtualWorld) const;
        void Deplete(std::uint32_t virtualWorld, GatheringPoint point);
        std::uint64_t RespawnDeadline(std::uint8_t kind, std::uint64_t worldMs);

        GatheringHooks _hooks;
        std::map<std::uint64_t, Player> _players;
        std::map<Cell, std::map<KCDC::Shared::Gathering::SourceKey, GatheringPoint>> _depleted;
        std::map<Cell, std::vector<KCDC::Shared::Game::GatheringSource>> _sources;
        std::uint64_t _sequence = 0;
        std::optional<GatheringRespawnTime> _respawnTime;
        std::mt19937_64 _random {std::random_device {}()};
    };
} // namespace KCDC::Features::Gathering
