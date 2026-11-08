#pragma once

#include "features/inventory/inventory.h"
#include "shared/features/crafting/smithing_rpc.h"
#include "smithing_evaluator.h"

#include <function2/function2.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::Crafting {
    /** Who is acting, where, and when -- read fresh by the caller for every request. */
    struct SmithingActor {
        std::uint64_t playerId = 0;
        std::uint64_t nowMs    = 0;
        glm::vec3 position {0.0F};
        std::uint32_t virtualWorld = 0;
        std::string level;
        std::uint16_t requiredDLC = 0;
        bool canAct               = false;
        // Absent when the character could not be evaluated; Begin requires it.
        std::optional<SmithingCharacterContext> character;
    };

    /** What the module reaches outside itself for. Every hook may be empty in a test. */
    struct SmithingHooks {
        fu2::function<KCDC::Features::Inventory::Inventory *(std::uint64_t playerId) const> inventory;
        // Worn units per inventory row; null when nothing is known worn.
        fu2::function<const std::map<std::string, std::uint32_t> *(std::uint64_t playerId) const> equipped;
        // True while the player holds another craft, an alchemy table.
        fu2::function<bool(std::uint64_t playerId) const> busy;
        // Synchronous vetoes: false refuses. A throw counts as a refusal.
        fu2::function<bool(std::uint64_t playerId, const nlohmann::json &proposal) const> starting;
        fu2::function<bool(std::uint64_t playerId, const nlohmann::json &proposal) const> completing;
        // Synchronous opt-in to return all held ingredients on an unfinished craft.
        fu2::function<bool(std::uint64_t playerId, const nlohmann::json &proposal) const> refunding;
        // Base finishing XP on the craftsmanship track; the owner applies its own multipliers.
        fu2::function<void(std::uint64_t playerId, double xp) const> experience;
        // A uniform draw in [0, bound). Without it the module draws from its own generator.
        fu2::function<std::uint32_t(std::uint32_t bound) const> random;
    };

    /** A crafting event for scripts, raised by the owner after inventory changes are dispatched. */
    struct SmithingEvent {
        std::string name;
        std::uint64_t playerId = 0;
        nlohmann::json payload;
    };

    /** A workpiece the server ended on its own. */
    struct SmithingClosure {
        std::uint64_t playerId                        = 0;
        std::uint32_t session                         = 0;
        KCDC::Shared::RPC::SmithingCloseReason reason = KCDC::Shared::RPC::SmithingCloseReason::Cancelled;
    };

    /** One request's answer: a code, and the workpiece it is about. */
    struct SmithingReply {
        KCDC::Shared::RPC::SmithingCode code = KCDC::Shared::RPC::SmithingCode::Accepted;
        std::uint32_t session                = 0;
    };

    /**
     * Server-side smithing for connected players, held in memory.
     *
     * The game's own minigame runs on the owner: the forge, the anvil and
     * every strike stay native, and the strike XP reaches the server through
     * the ordinary XP requests. What the server owns is the inventory. A
     * workpiece begins when the owner asks for a recipe at a smithery: the
     * server checks the station, the recipe perk and the craftsmanship skill,
     * and takes the materials out of the inventory before the owner may start
     * work. It ends once, as the minigame ended it:
     *
     * - success: the product the reported quality reaches is granted, with
     *   its native tier, condition and Hardened Steel buff, along with any
     *   lockpicks the character earns and the finishing XP;
     * - failure: the native share of each divisible material is spent, the
     *   rest goes back, drawn with the server's own coin;
     * - abandoned, before work could start: everything goes back.
     *
     * A disconnect, a script cancel, a timeout or leaving the smithery ends a
     * workpiece as a failure, which is what cancelling it at the anvil does.
     *
     * The reported quality is the one input the owner supplies. It can only
     * select among the recipe's authored products; a `craftingCompleting`
     * handler may refuse the result, which fails the workpiece.
     */
    class SmithingModule final {
      public:
        static constexpr std::uint64_t kSessionLifetimeMs = 30 * 60 * 1000;
        // The owner replays an approved recipe on its next frame. An abandon
        // later than this is a workpiece the anvil could have worked.
        static constexpr std::uint64_t kAbandonWindowMs = 5000;
        // The forge, the anvil and the quench tub stand around the smithery
        // entity; the minigame walks the player between them.
        static constexpr float kUseDistance = 6.0F;

        explicit SmithingModule(SmithingHooks hooks = {}): _hooks(std::move(hooks)) {}

        /** Takes the materials and opens a workpiece at the station, or says why not. */
        SmithingReply Begin(const SmithingActor &actor, const std::string &station, const std::string &recipe);
        /** Settles the player's workpiece as the minigame ended it. */
        SmithingReply Finish(const SmithingActor &actor, std::uint32_t session, KCDC::Shared::RPC::SmithingOutcome outcome, float quality);

        /** Ends the player's workpiece as a failure. False when there was nothing to end. */
        bool End(std::uint64_t playerId, const std::string &reason);
        /**
         * Fails every workpiece whose deadline passed or whose player can no
         * longer work at it. `locate` answers a player's current actor, or
         * nothing when they are gone. Returns what closed and why.
         */
        std::vector<SmithingClosure> Expire(std::uint64_t nowMs, const fu2::function<std::optional<SmithingActor>(std::uint64_t playerId) const> &locate);

        /** The workpiece the player has open, for the `closed` push. */
        std::optional<std::uint32_t> SessionOf(std::uint64_t playerId) const;
        nlohmann::json Inspect(std::uint64_t playerId, std::uint64_t nowMs) const;
        nlohmann::json Occupancy(const std::string &station, std::uint32_t virtualWorld, std::uint64_t nowMs) const;

        /** Units a refund could not put back, for the owner to log. */
        struct Unreturned {
            std::uint64_t playerId = 0;
            std::string itemClass;
            std::uint32_t amount = 0;
            std::string code;
        };
        std::vector<Unreturned> TakeUnreturned();

        /** Events raised since the last call, oldest first. */
        std::vector<SmithingEvent> TakeEvents();

      private:
        /** What happens to the materials when a workpiece closes. */
        enum class Settlement : std::uint8_t {
            // The product was granted: every unit taken was spent.
            Spent,
            // ConsumeRecipeMaterials(false): the native failure share is spent.
            Failed,
            // Nothing was worked: every unit goes back.
            Returned
        };
        /** Units taken from one inventory row, with what they were. */
        struct Taken {
            std::string itemClass;
            nlohmann::json metadata;
            std::uint32_t amount = 0;
        };
        std::set<std::uint64_t> _settling;

        struct Session {
            std::uint32_t id = 0;
            std::string station;
            std::uint32_t virtualWorld = 0;
            std::string level;
            std::uint16_t requiredDLC = 0;
            std::string recipe;
            // The recipe as Begin found it. The registry keeps every recipe it ever held, so a mod
            // restarted mid-forge does not change the rules of a workpiece already taken.
            const KCDC::Shared::Game::CraftRecipe *definition = nullptr;
            std::uint64_t startedMs                           = 0;
            std::uint64_t deadlineMs = 0;
            std::vector<Taken> taken;
            SmithingCharacterContext character;
        };

        static std::string Name(std::uint32_t session);
        std::optional<KCDC::Shared::RPC::SmithingCode> Admissible(const SmithingActor &actor, const std::string &station, const KCDC::Shared::Game::CraftRecipe *recipe) const;
        /** The unworn units the recipe's spent materials come out of, oldest rows first; nothing when the inventory lacks one. */
        std::optional<std::vector<KCDC::Features::Inventory::InventoryUnit>> Materials(std::uint64_t playerId, const KCDC::Features::Inventory::Inventory &inventory, const KCDC::Shared::Game::CraftRecipe &recipe) const;
        std::uint32_t Draw(std::uint32_t bound);
        /** Adds the product and the lockpicks, zeroing `lockpicks` when they found no room; false when the inventory refused the product. */
        bool Grant(std::uint64_t playerId, const SmithingProduct &product, std::uint32_t &lockpicks, nlohmann::json &outputs);
        /** Closes the workpiece, returns what the settlement leaves and raises `craftingEnded`. */
        void Retire(std::uint64_t playerId, const std::string &outcome, const std::string &reason, Settlement settlement);

        SmithingHooks _hooks;
        std::uint32_t _nextSession = 0;
        std::uint64_t _generator   = 0x9E3779B97F4A7C15ULL;
        std::map<std::uint64_t, Session> _sessions;
        std::map<std::pair<std::string, std::uint32_t>, std::uint64_t> _stations;
        std::vector<SmithingEvent> _events;
        std::vector<Unreturned> _unreturned;
    };
} // namespace KCDC::Features::Crafting
