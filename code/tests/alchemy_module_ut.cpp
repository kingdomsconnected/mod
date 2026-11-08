#include "features/crafting/alchemy_module.h"
#include "features/inventory/inventory.h"
#include "shared/game/alchemy_animations.h"
#include "shared/game/crafting_catalog.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
    constexpr const char *kSage   = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    constexpr std::uint64_t kPlayer = 7;
    constexpr std::uint64_t kRival  = 8;

    void RequireAlchemy(bool condition, const std::string &message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    KCDC::Features::Crafting::AlchemyCharacterContext Character(std::uint64_t worldMilliseconds = 15000, bool blackArts = false) {
        KCDC::Features::Player::SoulSnapshot soul;
        soul.clock                   = {1000, worldMilliseconds};
        soul.profile.health          = 100.0F;
        soul.profile.skillLevels[6] = 15;
        if (blackArts) {
            soul.profile.perks.insert("234615f0-2e62-4977-aeaf-da2da47e7847");
        }
        KCDC::Features::Crafting::AlchemyCharacterContext context;
        std::string error;
        RequireAlchemy(KCDC::Features::Crafting::AlchemyCharacterEvaluator::Evaluate(soul, context, error), "fixture character evaluates: " + error);
        return context;
    }

    /** One player at the first authored alchemy table, with a stocked in-memory inventory. */
    struct AlchemyFixture {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory {ids};
        KCDC::Features::Inventory::Inventory rivalInventory {ids};
        KCDC::Features::Crafting::AlchemyModule alchemy;
        KCDC::Features::Crafting::AlchemyActor actor;
        std::string station;
        std::string sage;
        std::vector<double> xp;
        std::vector<nlohmann::json> starting;
        std::vector<nlohmann::json> completing;
        std::vector<nlohmann::json> refunds;
        bool refundInterruptions = false;
        bool reenterRefund       = false;
        bool throwRefund         = false;
        bool allowStart    = true;
        bool allowComplete = true;
        bool endOnComplete = false;

        AlchemyFixture() {
            KCDC::Features::Crafting::AlchemyHooks hooks;
            hooks.inventory = [this](std::uint64_t playerId) {
                return playerId == kPlayer ? &inventory : playerId == kRival ? &rivalInventory : nullptr;
            };
            hooks.starting = [this](std::uint64_t, const nlohmann::json &proposal) {
                starting.push_back(proposal);
                return allowStart;
            };
            hooks.completing = [this](std::uint64_t playerId, const nlohmann::json &proposal) {
                completing.push_back(proposal);
                if (endOnComplete) {
                    alchemy.End(playerId, "cancelled");
                }
                return allowComplete;
            };
            hooks.refunding = [this](std::uint64_t playerId, const nlohmann::json &proposal) {
                refunds.push_back(proposal);
                if (reenterRefund) {
                    RequireAlchemy(!alchemy.End(playerId, "cancelled") && alchemy.Start(actor, "nested", station, "").code == "playerBusy", "settlement cannot be reentered");
                }
                if (throwRefund) {
                    throw std::runtime_error("refund policy failed");
                }
                return refundInterruptions && proposal.at("reason") == "interrupted";
            };
            hooks.experience = [this](std::uint64_t, double amount) {
                xp.push_back(amount);
            };
            alchemy         = KCDC::Features::Crafting::AlchemyModule(std::move(hooks));
            const auto *row = &*std::find_if(KCDC::Shared::Game::g_CraftStations.begin(), KCDC::Shared::Game::g_CraftStations.end(), [](const auto &entry) {
                return entry.kind == KCDC::Shared::Game::CraftKind::Alchemy;
            });
            station            = std::string(row->id);
            actor.playerId     = kPlayer;
            actor.nowMs        = 100000;
            actor.position     = {row->x, row->y, row->z};
            actor.virtualWorld = 3;
            actor.level        = std::string(row->level);
            actor.canAct       = true;
            actor.character    = Character();
            const auto added   = inventory.Add(kSage, 30, nlohmann::json::object(), std::nullopt);
            RequireAlchemy(added.ok && added.outputs.size() == 1, "fixture stock: " + added.code);
            sage = added.outputs.front().id;
        }

        std::uint32_t Sage() const {
            std::uint32_t amount = 0;
            for (const auto &[id, row] : inventory.Rows()) {
                if (row.itemClass == kSage) {
                    amount += row.amount;
                }
            }
            return amount;
        }

        KCDC::Features::Crafting::AlchemyReply Current() const {
            return alchemy.Current(kPlayer);
        }

        /** Waits out the last action's authored animation, as the owner would. */
        void Wait() {
            const auto current = Current();
            if (current.state.is_object()) {
                actor.nowMs = std::max(actor.nowMs, current.state.at("readyAtMs").get<std::uint64_t>());
            }
        }

        KCDC::Features::Crafting::AlchemyReply Select(KCDC::Shared::Crafting::AlchemyPosition target, const std::string &item = "") {
            Wait();
            const auto current = Current();
            return alchemy.Select(actor, current.session, current.sequence + 1, static_cast<std::uint32_t>(target), item);
        }

        void Require(KCDC::Features::Crafting::AlchemyReply reply, const std::string &what) {
            RequireAlchemy(reply.ok, what + ": " + reply.code);
        }

        void Open(const std::string &continuationOf = "") {
            Require(alchemy.Start(actor, "1", station, continuationOf), "start");
            if (continuationOf.empty()) {
                Wait();
                Require(alchemy.EntryReady(actor, Current().session, 1), "entry");
            }
        }

        /** Water, one sage, into the pot, then the flask: a staged finish. */
        void Brew() {
            Require(Select(KCDC::Shared::Crafting::AlchemyPosition::Glass4), "water base");
            Require(Select(KCDC::Shared::Crafting::AlchemyPosition::Herb1, sage), "take sage");
            Require(Select(KCDC::Shared::Crafting::AlchemyPosition::Pot), "sage into pot");
            Require(Select(KCDC::Shared::Crafting::AlchemyPosition::PotionFlask), "take flask");
            Require(Select(KCDC::Shared::Crafting::AlchemyPosition::Pot), "finish from pot");
        }

        std::vector<std::string> EventNames() {
            std::vector<std::string> names;
            for (const auto &event : alchemy.TakeEvents()) {
                names.push_back(event.name);
            }
            return names;
        }
    };

    void EntryTests() {
        AlchemyFixture test;
        auto started = test.alchemy.Start(test.actor, "1", test.station, "");
        RequireAlchemy(started.ok && started.session != "" && started.state.at("readyAtMs").get<std::uint64_t>() > test.actor.nowMs && test.starting.size() == 1 && test.starting.front().at("station") == test.station, "a table opens behind its authored opening");
        RequireAlchemy(test.alchemy.Select(test.actor, started.session, 1, static_cast<std::uint32_t>(KCDC::Shared::Crafting::AlchemyPosition::Glass4), "").code == "alchemyEntryPending", "nothing happens at the table before the opening finishes");
        RequireAlchemy(test.alchemy.EntryReady(test.actor, started.session, 1).code == "alchemyActionTooEarly", "the opening animation cannot be skipped");
        test.Wait();
        RequireAlchemy(test.alchemy.EntryReady(test.actor, started.session, 2).code == "invalidAlchemyEntryPhase", "entry has one exact sequence");
        test.Require(test.alchemy.EntryReady(test.actor, started.session, 1), "entry");
        RequireAlchemy(test.EventNames() == std::vector<std::string> {"craftingStarted"}, "entry raises craftingStarted");
        RequireAlchemy(test.alchemy.Start(test.actor, "2", test.station, "").code == "playerBusy", "one session per player");

        auto rival     = test.actor;
        rival.playerId = kRival;
        RequireAlchemy(test.alchemy.Start(rival, "1", test.station, "").code == "stationBusy", "one session per table");
        rival.virtualWorld = 4;
        RequireAlchemy(test.alchemy.Start(rival, "1", test.station, "").ok, "the same table in another world is another table");
        const auto occupancy = test.alchemy.Occupancy(test.station, 3, test.actor.nowMs);
        RequireAlchemy(occupancy.at("occupied") == true && occupancy.at("occupant").at("playerId") == kPlayer && occupancy.at("occupant").at("phase") == "brewing", "occupancy names the owner");

        const auto current = test.Current();
        RequireAlchemy(test.alchemy.Select(test.actor, current.session, current.sequence + 2, static_cast<std::uint32_t>(KCDC::Shared::Crafting::AlchemyPosition::Glass4), "").code == "invalidAlchemySequence", "an action cannot skip a sequence");
        auto away        = test.actor;
        away.position.x += 10.0F;
        RequireAlchemy(test.alchemy.Select(away, current.session, current.sequence + 1, static_cast<std::uint32_t>(KCDC::Shared::Crafting::AlchemyPosition::Glass4), "").code == "alchemyNotAuthorized", "a player away from the table cannot act at it");
        test.Require(test.Select(KCDC::Shared::Crafting::AlchemyPosition::Glass4), "water base");
        const auto after = test.Current();
        RequireAlchemy(test.alchemy.Select(test.actor, after.session, after.sequence + 1, static_cast<std::uint32_t>(KCDC::Shared::Crafting::AlchemyPosition::Herb1), test.sage).code == "alchemyActionTooEarly", "an action waits for the last one's authored animation");

        AlchemyFixture vetoed;
        vetoed.allowStart = false;
        const auto refused = vetoed.alchemy.Start(vetoed.actor, "1", vetoed.station, "");
        RequireAlchemy(refused.code == "craftingStartingRejected" && !refused.state.is_object() && vetoed.alchemy.Occupancy(vetoed.station, 3, 0).at("occupied") == false, "a refused start leaves no session and no table");
        vetoed.actor.character.reset();
        vetoed.allowStart = true;
        RequireAlchemy(vetoed.alchemy.Start(vetoed.actor, "1", vetoed.station, "").code == "alchemyCharacterUnavailable", "no character, no table");
    }

    void IngredientTests() {
        AlchemyFixture test;
        test.Open();
        const auto before = test.inventory.Revision();
        test.Require(test.Select(KCDC::Shared::Crafting::AlchemyPosition::Herb1, test.sage), "take sage");
        RequireAlchemy(test.Sage() == 29 && test.inventory.Revision() == before + 1, "taking an ingredient removes one unit from the inventory");
        const auto state = test.Current().state;
        RequireAlchemy(state.at("resources").size() == 1 && state.at("resources")[0].at("itemId") == test.sage && state.at("resources")[0].at("position") == 7 && state.at("held") == 7, "the taken unit is on the table, named by its row");
        test.Wait();
        const auto current = test.Current();
        test.Require(test.alchemy.Return(test.actor, current.session, current.sequence + 1), "return sage");
        RequireAlchemy(test.Sage() == 30 && test.Current().state.at("resources").empty(), "an explicit return puts the unit back");
        RequireAlchemy(!test.Select(KCDC::Shared::Crafting::AlchemyPosition::Herb1, "no-such-row").ok, "an unknown row cannot be taken");

        // One sage in the bowl, unmilled: comes back. One milled in the mortar: spent.
        test.Require(test.Select(KCDC::Shared::Crafting::AlchemyPosition::Herb1, test.sage), "take sage");
        test.Require(test.Select(KCDC::Shared::Crafting::AlchemyPosition::Bowl), "sage into bowl");
        test.Require(test.Select(KCDC::Shared::Crafting::AlchemyPosition::Herb1, test.sage), "take sage");
        test.Require(test.Select(KCDC::Shared::Crafting::AlchemyPosition::Mortar), "sage into mortar");
        test.Require(test.Select(KCDC::Shared::Crafting::AlchemyPosition::Mortar), "mill");
        RequireAlchemy(test.Sage() == 28, "both units left the inventory");
        test.alchemy.TakeEvents();
        const auto cancelled = test.alchemy.Cancel(test.actor, test.Current().session, "", "");
        RequireAlchemy(cancelled.ok && cancelled.state.at("terminal") == true && cancelled.state.at("tableClosed") == true && cancelled.state.at("outcome") == "cancelled" && cancelled.state.at("retiredResourceIds").size() == 1, "cancel ends the session and closes the table");
        RequireAlchemy(test.Sage() == 29, "the wholly unmilled bowl group is refunded, the milled mortar group is spent");
        const auto events = test.alchemy.TakeEvents();
        RequireAlchemy(events.size() == 1 && events[0].name == "craftingEnded" && events[0].payload.at("outcome") == "cancelled" && events[0].payload.at("refunded").size() == 1 && events[0].payload.at("refunded")[0].at("amount") == 1, "craftingEnded names the refund");
        RequireAlchemy(test.alchemy.Occupancy(test.station, 3, 0).at("occupied") == false, "cancel releases the table");
    }

    void CompletionTests() {
        AlchemyFixture test;
        test.actor.character = Character(16199000, true);
        test.Open();
        RequireAlchemy(test.alchemy.EntryBonus(kPlayer) == std::optional<float>(0.1F), "entry captures the table bonus");
        test.Brew();
        auto pending = test.Current();
        RequireAlchemy(pending.state.at("pendingTerminal") == true && pending.state.at("terminal") == true && test.Sage() == 29, "the finish is staged, not settled");
        RequireAlchemy(test.alchemy.Complete(test.actor, pending.session, pending.sequence).code == "alchemyActionTooEarly", "settlement waits for the finish animation");
        RequireAlchemy(test.alchemy.Complete(test.actor, pending.session, pending.sequence + 1).code == "invalidAlchemyTerminal", "settlement binds the staged sequence");
        test.Wait();
        test.alchemy.TakeEvents();
        const auto rows      = test.inventory.Rows().size();
        const auto completed = test.alchemy.Complete(test.actor, pending.session, pending.sequence);
        RequireAlchemy(completed.ok && completed.state.at("terminal") == true && completed.state.at("pendingTerminal") == false && completed.state.at("tableClosed") == false, "a completed batch keeps the table");
        RequireAlchemy(test.completing.size() == 1 && test.completing[0].contains("product") && test.completing[0].contains("quality") && test.completing[0].at("xp").get<double>() > 0.0, "the result is reviewed before it is granted");
        RequireAlchemy(test.inventory.Rows().size() == rows + 1 && test.Sage() == 29 && test.xp.size() == 1 && test.xp[0] == test.completing[0].at("xp").get<double>(), "the output is added, the sage stays spent, the XP is ordered");
        const auto events = test.alchemy.TakeEvents();
        RequireAlchemy(events.size() == 2 && events[0].name == "craftingCompleted" && events[1].name == "craftingEnded" && events[0].payload.at("outputs").size() == 1 && events[0].payload.at("knowledge").is_object(), "completed then ended");
        RequireAlchemy(test.alchemy.Occupancy(test.station, 3, 0).at("occupant").at("phase") == "betweenBatches", "the table waits between batches");

        // The next batch keeps the bonus its visit captured, even though the
        // character's own bonus has since lapsed with the daily window.
        test.actor.character = Character(16201000, true);
        RequireAlchemy(test.actor.character->sessionQualityBonus == 0.0F, "the daily window closed");
        RequireAlchemy(test.alchemy.Start(test.actor, "2", test.station, "not-the-last-batch").code == "staleAlchemyTable", "a continuation names the batch that just finished");
        test.Open(completed.session);
        RequireAlchemy(test.EventNames() == std::vector<std::string> {"craftingStarted"} && test.Current().state.at("readyAtMs") == test.actor.nowMs, "the next batch starts at once, without another opening");
        RequireAlchemy(test.alchemy.EntryBonus(kPlayer) == std::optional<float>(0.1F), "continuation keeps the entry bonus");
        test.Brew();
        test.Wait();
        test.allowComplete = false;
        pending            = test.Current();
        const auto before  = test.inventory.Rows();
        const auto vetoed  = test.alchemy.Complete(test.actor, pending.session, pending.sequence);
        RequireAlchemy(!vetoed.ok && vetoed.code == "craftingCompletingRejected" && vetoed.state.at("terminal") == true && vetoed.state.at("outcome") == "failed" && vetoed.state.at("tableClosed") == true, "a refused result ends the batch as failed and closes the table");
        RequireAlchemy(test.inventory.Rows() == before && test.xp.size() == 1 && test.Sage() == 28, "a refused result grants nothing and refunds nothing it spent");
        const auto ended = test.alchemy.TakeEvents();
        RequireAlchemy(ended.size() == 1 && ended[0].name == "craftingEnded" && ended[0].payload.at("reason") == "craftingCompletingRejected", "a refusal raises only craftingEnded");

        AlchemyFixture cancelled;
        cancelled.Open();
        cancelled.Brew();
        cancelled.Wait();
        cancelled.endOnComplete = true;
        const auto staged       = cancelled.Current();
        RequireAlchemy(cancelled.alchemy.Complete(cancelled.actor, staged.session, staged.sequence).code == "alchemyCompletionSuperseded" && cancelled.xp.empty(), "a handler that ends the session wins over the result");
    }

    void RefundTests() {
        for (const char *reason : {"interrupted", "cancelled", "clientError"}) {
            AlchemyFixture test;
            test.refundInterruptions = true;
            test.reenterRefund       = true;
            test.Open();
            test.Brew(); // The herb has been processed; the native rule returns none.
            const auto session = test.Current().session;
            test.Require(test.alchemy.Cancel(test.actor, session, "", "", reason), "cancel with reason");
            RequireAlchemy(test.refunds.size() == 1 && test.refunds[0].at("reason") == reason && test.refunds[0].at("materials").size() == 1, "policy sees the ingredient but not the station's water");
            RequireAlchemy(test.Sage() == (std::string(reason) == "interrupted" ? 30U : 29U), "only the interruption policy restores processed herbs");
            const auto settled = test.inventory.Rows();
            test.alchemy.Cancel(test.actor, session, "", "", reason);
            RequireAlchemy(test.inventory.Rows() == settled && test.refunds.size() == 1 && test.xp.empty(), "duplicate cancellation grants no items or XP");
            RequireAlchemy(test.alchemy.TakeEvents().back().payload.at("reason") == reason, "terminal preserves the cancellation reason");
        }
        for (const auto *reason : {"contextInvalidated", "timeout"}) {
            AlchemyFixture closed;
            closed.refundInterruptions = true;
            closed.Open();
            closed.Brew();
            const auto session = closed.Current().session;
            if (std::string(reason) == "timeout") {
                closed.actor.nowMs += 30 * 60 * 1000;
            }
            else {
                closed.actor.canAct = false;
            }
            RequireAlchemy(!closed.alchemy.Cancel(closed.actor, "stale-session", "", "", "interrupted").ok && closed.refunds.empty(), "a stale report cannot close the current batch");
            RequireAlchemy(!closed.alchemy.Cancel(closed.actor, session, "", "", "interrupted").ok, "server closure overrides a client interruption");
            RequireAlchemy(closed.refunds.size() == 1 && closed.refunds[0].at("reason") == reason && closed.Sage() == 29, "server closure keeps the native material loss");
            RequireAlchemy(closed.alchemy.Inspect(kPlayer, closed.actor.nowMs).is_null(), "server closure releases the table");
        }
        AlchemyFixture returned;
        returned.refundInterruptions = true;
        returned.Open();
        returned.Require(returned.Select(KCDC::Shared::Crafting::AlchemyPosition::Herb1, returned.sage), "take");
        returned.Wait();
        returned.Require(returned.alchemy.Return(returned.actor, returned.Current().session, returned.Current().sequence + 1), "return");
        returned.alchemy.Cancel(returned.actor, returned.Current().session, "", "", "interrupted");
        RequireAlchemy(returned.Sage() == 30 && returned.refunds[0].at("materials").empty(), "an explicitly returned herb is never refunded twice");

        AlchemyFixture fault;
        fault.throwRefund = true;
        fault.Open();
        fault.Brew();
        fault.alchemy.End(kPlayer, "interrupted");
        RequireAlchemy(fault.Sage() == 29 && fault.alchemy.Inspect(kPlayer, fault.actor.nowMs).is_null(), "throwing policy keeps native losses and closes the table");

        AlchemyFixture completed;
        completed.refundInterruptions = true;
        completed.Open();
        completed.Brew();
        completed.Wait();
        completed.Require(completed.alchemy.Complete(completed.actor, completed.Current().session, completed.Current().sequence), "complete");
        completed.alchemy.Cancel(completed.actor, "", "", completed.Current().session, "interrupted");
        RequireAlchemy(completed.refunds.empty() && completed.Sage() == 29, "a finished batch cannot refund its spent herb");
    }

    void LifecycleTests() {
        AlchemyFixture test;
        test.Open();
        test.Require(test.Select(KCDC::Shared::Crafting::AlchemyPosition::Herb1, test.sage), "take sage");
        const auto closed = test.alchemy.Expire(test.actor.nowMs, [](std::uint64_t) {
            return std::optional<KCDC::Features::Crafting::AlchemyActor>();
        });
        RequireAlchemy(closed.size() == 1 && closed[0].first == kPlayer && closed[0].second == "contextInvalidated" && test.Sage() == 30, "a player who is gone loses the session and gets the herb back");

        test.Open();
        const auto deadline = test.actor.nowMs + KCDC::Features::Crafting::AlchemyModule::kSessionLifetimeMs;
        const auto expired  = test.alchemy.Expire(deadline, [&test](std::uint64_t) {
            return std::optional<KCDC::Features::Crafting::AlchemyActor>(test.actor);
        });
        RequireAlchemy(expired.size() == 1 && expired[0].second == "timeout" && !test.alchemy.Inspect(kPlayer, deadline).is_object(), "a session times out");

        test.Open();
        RequireAlchemy(test.alchemy.End(kPlayer, "cancelled") && !test.alchemy.End(kPlayer, "cancelled"), "a script ends a session once");

        RequireAlchemy(test.alchemy.Knowledge(kPlayer).empty(), "knowledge starts empty");
        const auto recipe = std::find_if(KCDC::Shared::Game::g_CraftRecipes.begin(), KCDC::Shared::Game::g_CraftRecipes.end(), [](const auto &row) {
            return row.kind == KCDC::Shared::Game::CraftKind::Alchemy;
        });
        const std::string id(recipe->id);
        RequireAlchemy(test.alchemy.SetKnowledge(kPlayer, id, 5) && test.alchemy.Knowledge(kPlayer).at(id) == 5, "a script sets a recipe's known steps");
        RequireAlchemy(!test.alchemy.SetKnowledge(kPlayer, id, KCDC::Features::Crafting::AlchemyModule::kRecipeKnowledgeMask + 1) && !test.alchemy.SetKnowledge(kPlayer, "not-a-recipe", 1), "only real recipes and masks");
        RequireAlchemy(test.alchemy.SetKnowledge(kPlayer, id, 0) && test.alchemy.Knowledge(kPlayer).empty(), "0 forgets a recipe");
        test.alchemy.SetKnowledge(kPlayer, id, 5);
        test.alchemy.Forget(kPlayer);
        RequireAlchemy(test.alchemy.Knowledge(kPlayer).empty(), "knowledge leaves with the player");
    }
} // namespace

void AlchemyModuleTests() {
    EntryTests();
    IngredientTests();
    CompletionTests();
    LifecycleTests();
    RefundTests();
}
