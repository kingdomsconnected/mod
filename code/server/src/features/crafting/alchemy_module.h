#pragma once

#include "alchemy_character_evaluator.h"
#include "features/inventory/inventory.h"
#include "shared/features/crafting/alchemy_native_ids.h"

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
    /**
     * The brewing rules one session runs on. The table values come from
     * AlchemyTable.lua and rpg_param.xml in the shipped catalog; the rest is
     * the character's, refreshed from every evaluation the caller supplies.
     */
    struct AlchemyRules {
        float boilingTolerance          = 0.0F;
        float freshHerbQuality          = 0.0F;
        float driedHerbQuality          = 0.3F;
        float decayedHerbQuality        = 0.01F;
        float failureXP                 = 0.0F;
        std::uint32_t allowedMistakes   = 4;
        float timeUnitSeconds           = 10.0F;
        float weakBoilTimeout           = 5.0F;
        float strongBoilTimeout         = 10.0F;
        float baseQualityWeight         = 0.0F;
        float resourceQualityWeight     = 0.2F;
        float firstQualityThreshold     = 0.9F;
        float secondQualityThreshold    = 0.7F;
        float secondQualityXPMultiplier = 1.35F;
        float thirdQualityXPMultiplier  = 2.0F;
        // wh_pl_BellowsUseCountToDistill default 2, native 182E16834 clamps 1..100.
        std::uint32_t distillationPumps = KCDC::Shared::Crafting::AlchemyNative::kDefaultDistillationPumps;
    };

    /** Who is acting, where, and when -- read fresh by the caller for every request. */
    struct AlchemyActor {
        std::uint64_t playerId = 0;
        std::uint64_t nowMs    = 0;
        glm::vec3 position {0.0F};
        std::uint32_t virtualWorld = 0;
        std::string level;
        std::uint16_t requiredDLC = 0;
        bool canAct               = false;
        // Absent when the character could not be evaluated. Start and entry
        // require it; a later action keeps the one it last had.
        std::optional<AlchemyCharacterContext> character;
    };

    /** What the module reaches outside itself for. Every hook may be empty in a test. */
    struct AlchemyHooks {
        fu2::function<KCDC::Features::Inventory::Inventory *(std::uint64_t playerId) const> inventory;
        // Synchronous vetoes: false refuses. A throw counts as a refusal.
        fu2::function<bool(std::uint64_t playerId, const nlohmann::json &proposal) const> starting;
        fu2::function<bool(std::uint64_t playerId, const nlohmann::json &proposal) const> completing;
        // Synchronous opt-in to return all held ingredients on an unfinished craft.
        fu2::function<bool(std::uint64_t playerId, const nlohmann::json &proposal) const> refunding;
        // Base brewing XP on the alchemy track; the owner applies its own multipliers.
        fu2::function<void(std::uint64_t playerId, double xp) const> experience;
    };

    enum class AlchemyActionKind : std::uint8_t {
        TakeIngredient,
        ReturnIngredient,
        SelectBase,
        MoveContents,
        Mill,
        PutPotOnFire,
        RemovePotFromFire,
        Wait,
        TakeObject,
        FinishFromPot,
        FinishFromMortar,
        SelectObject,
        ReturnObject,
        ReturnInput,
        Pulse,
        BeginDistillation
    };

    /** A native input as the server resolves it. No durations, counts, quality, XP or outputs. */
    struct AlchemyAction {
        std::string session;
        std::uint64_t sequence = 0;
        AlchemyActionKind kind = AlchemyActionKind::Wait;
        std::string itemId;
        std::uint32_t resourceId                            = 0;
        KCDC::Shared::Crafting::AlchemyPosition source      = KCDC::Shared::Crafting::AlchemyPosition::Bowl;
        KCDC::Shared::Crafting::AlchemyPosition destination = KCDC::Shared::Crafting::AlchemyPosition::Pot;
    };

    /** A crafting event for scripts, raised by the owner after inventory changes are dispatched. */
    struct AlchemyEvent {
        std::string name;
        std::uint64_t playerId = 0;
        nlohmann::json payload;
    };

    /** One request's answer. `state` is the response's state document, null when the player has no session. */
    struct AlchemyReply {
        bool ok = false;
        std::string code;
        std::string session;
        std::uint64_t sequence = 0;
        nlohmann::json state;
    };

    /**
     * Server-side alchemy for connected players, held in memory.
     *
     * One session per player and per table. A session is the server's copy
     * of the native working table: which ingredients sit where, the pot's
     * heat and each ingredient's boiling exposure, simulated on the server's
     * own clock from the native actions the owner asks for. The owner only
     * ever names a target; the server decides what that does, how long its
     * authored animation takes, and at the end which potion, how many, at
     * what quality and for how much XP.
     *
     * Ingredients leave the player's inventory when taken to the table. The
     * native reset's rule decides what comes back: an explicit return, or
     * any wholly unmilled Bowl, Mortar or herb-hand group when the session
     * ends; everything else was spent. A finished batch keeps the table for
     * the same player, so the next batch can start without leaving it.
     *
     * Nothing here persists. Recipe knowledge lives here for the player's
     * session; a script saves it and restores it with `SetKnowledge`.
     */
    class AlchemyModule final {
      public:
        static constexpr std::size_t kMaximumResources      = 64;
        static constexpr std::uint32_t kMaximumProductYield = 64;
        static constexpr std::uint32_t kRecipeKnowledgeMask = KCDC::Shared::Crafting::AlchemyNative::kCompleteRecipeKnowledge;
        static constexpr std::uint64_t kMaximumActions      = 4096;
        static constexpr std::uint64_t kSessionLifetimeMs   = 30 * 60 * 1000;
        static constexpr float kUseDistance                 = 3.25F;

        explicit AlchemyModule(AlchemyHooks hooks = {}): _hooks(std::move(hooks)) {}

        /** Opens a table, or with `continuationOf` brews the next batch at the table the player kept. */
        AlchemyReply Start(const AlchemyActor &actor, const std::string &request, const std::string &station, const std::string &continuationOf);
        /** The native opening animation finished: the entry quality bonus is captured here. */
        AlchemyReply EntryReady(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence);
        /** A native target selection; the action it means is the server's to derive. */
        AlchemyReply Select(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence, std::uint32_t target, const std::string &itemId);
        /** The native Back action on whatever is held. */
        AlchemyReply Return(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence);
        /** One pump of the held bellows. */
        AlchemyReply Pulse(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence);
        /** Settles a staged finish once its authored animation has played. */
        AlchemyReply Complete(const AlchemyActor &actor, const std::string &session, std::uint64_t sequence);
        /** The owner left the table: ends the named session or start, or closes the table kept between batches. */
        AlchemyReply Cancel(const AlchemyActor &actor, const std::string &session, const std::string &startRequest, const std::string &continuationOf, const std::string &reason = "cancelled");

        /** Ends the player's session with refunds and releases their table. False when there was nothing to end. */
        bool End(std::uint64_t playerId, const std::string &reason);
        /**
         * Ends every session and releases every table whose deadline passed
         * or whose player can no longer use it. `locate` answers a player's
         * current actor, or nothing when they are gone. Returns who was
         * closed and why.
         */
        std::vector<std::pair<std::uint64_t, std::string>> Expire(std::uint64_t nowMs, const fu2::function<std::optional<AlchemyActor>(std::uint64_t playerId) const> &locate);
        /** Drops what the module still holds for a player who has left. */
        void Forget(std::uint64_t playerId);

        /** The player's current session and state, as a request's answer would carry them. */
        AlchemyReply Current(std::uint64_t playerId) const;
        /** The table-entry bonus the player's current visit captured, if it has. */
        std::optional<float> EntryBonus(std::uint64_t playerId) const;

        nlohmann::json Inspect(std::uint64_t playerId, std::uint64_t nowMs) const;
        nlohmann::json Occupancy(const std::string &station, std::uint32_t virtualWorld, std::uint64_t nowMs) const;
        nlohmann::json Knowledge(std::uint64_t playerId) const;
        bool SetKnowledge(std::uint64_t playerId, const std::string &recipe, std::uint32_t mask);

        /** Events raised since the last call, oldest first. */
        std::vector<AlchemyEvent> TakeEvents();

      private:
        std::set<std::uint64_t> _settling;

        struct Session {
            nlohmann::json state;
            // The last character evaluation this session was given.
            AlchemyCharacterContext character;
        };
        struct Table {
            std::uint64_t playerId = 0;
            std::string entryId;
            // The batch now running, or the last one to finish.
            std::string session;
            std::uint64_t deadlineMs = 0;
            std::optional<float> entryBonus;
        };
        struct Terminal {
            std::string session;
            std::uint64_t sequence = 0;
            nlohmann::json state;
        };

        AlchemyReply Answer(std::uint64_t playerId, bool ok, const std::string &code) const;
        Session *FindSession(std::uint64_t playerId, const std::string &session);
        std::map<std::pair<std::string, std::uint32_t>, Table>::iterator TableOf(std::uint64_t playerId);
        std::string Admissible(const AlchemyActor &actor, const std::string &station, const std::string &continuationOf) const;
        AlchemyReply Apply(const AlchemyActor &actor, const AlchemyAction &action);
        AlchemyReply Finish(std::uint64_t playerId, Session &held, nlohmann::json state);
        /** Ends the active session: refunds what the native reset returns, records the terminal, raises craftingEnded. */
        void Retire(std::uint64_t playerId, const std::string &outcome, const std::string &reason, bool closeTable, std::uint64_t sequence);
        void CloseTable(std::uint64_t playerId);
        nlohmann::json Refund(std::uint64_t playerId, const nlohmann::json &resource);

        AlchemyHooks _hooks;
        std::uint64_t _nextSession = 0;
        std::map<std::uint64_t, Session> _sessions;
        std::map<std::pair<std::string, std::uint32_t>, Table> _tables;
        std::map<std::uint64_t, Terminal> _terminals;
        std::map<std::uint64_t, std::map<std::string, std::uint32_t>> _knowledge;
        std::vector<AlchemyEvent> _events;
    };
} // namespace KCDC::Features::Crafting
