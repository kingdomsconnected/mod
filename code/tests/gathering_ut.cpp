#include "features/gathering/gathering_module.h"
#include "shared/features/gathering/gathering_rules.h"
#include "shared/game/gathering_catalog.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
    constexpr std::uint64_t kPlayer = 51;
    constexpr std::uint8_t kNettle  = 2;
    const glm::vec3 kSource(100.0F, 200.0F, 30.0F);
    const glm::vec3 kBeside(101.0F, 200.0F, 30.0F);

    void RequireGathering(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    struct Fixture {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory {ids};
        bool allow                 = true;
        double xp                  = 0.0;
        std::optional<float> level = 4.0F;
        KCDC::Features::Gathering::GatheringModule gathering {Hooks()};

        KCDC::Features::Gathering::GatheringHooks Hooks() {
            KCDC::Features::Gathering::GatheringHooks hooks;
            hooks.inventory = [this](std::uint64_t) {
                return &inventory;
            };
            hooks.harvesting = [this](std::uint64_t, const nlohmann::json &) {
                return allow;
            };
            hooks.experience = [this](std::uint64_t, double amount) {
                xp += amount;
            };
            hooks.survivalLevel = [this](std::uint64_t) {
                return level;
            };
            return hooks;
        }

        KCDC::Features::Gathering::GatheringRequest Begin(const char *id, std::uint8_t kind = kNettle) const {
            return {"begin", id, kind, kSource, {}};
        }
        KCDC::Features::Gathering::GatheringRequest Complete(const char *id, std::vector<glm::vec3> sources = {}) const {
            return {"complete", id, 0, glm::vec3(0.0F), std::move(sources)};
        }
    };

    void RulesMatchTheGame() {
        RequireGathering(KCDC::Shared::Gathering::Radius(1.0F) > 0.0F && KCDC::Shared::Gathering::InHarvestRadius(0.1F, 0.1F, 0.2F) && !KCDC::Shared::Gathering::InHarvestRadius(0.3F, 0.0F, 0.2F), "harvest radius follows native planar semantics");
        RequireGathering(KCDC::Shared::Gathering::CalculateYield(4.0F, 1, 1) == 1 && KCDC::Shared::Gathering::CalculateYield(16.0F, 2, 2) == 5, "yield formula matches native slot 18 truncation");
        RequireGathering(!KCDC::Shared::Gathering::WithinDistance(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 3.0F), "a NaN position never passes a distance check");
    }

    void HarvestGrantsAndDepletes() {
        Fixture test;
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("a1"), kBeside, 0, 1000, 5000).empty(), "a known kind within reach begins");
        RequireGathering(test.gathering.Complete(kPlayer, test.Complete("a1"), kBeside, 0, true, 1200, 5000).code == "tooEarly", "the native animation cannot be skipped");
        const auto harvest = test.gathering.Complete(kPlayer, test.Complete("a1"), kBeside, 0, true, 3000, 5000);
        const auto *kind   = KCDC::Shared::Game::FindGatheringKind(kNettle);
        RequireGathering(harvest.code.empty() && harvest.amount == KCDC::Shared::Gathering::CalculateYield(4.0F, 1, kind->amount), "the yield follows the player's survival level");
        RequireGathering(test.inventory.Rows().size() == 1 && test.inventory.Rows().begin()->second.itemClass == kind->itemClass && test.inventory.Rows().begin()->second.amount == harvest.amount, "the herb is granted");
        const auto changes = test.inventory.TakeChanges();
        RequireGathering(changes.size() == 1 && changes[0].reason == "gather" && test.xp > 0.0, "the grant is recorded as gathering and pays survival XP");
        RequireGathering(harvest.depleted.size() == 1 && harvest.depleted[0].position == kSource, "the harvest names what it depleted, for nearby players to hide");

        const auto until = 5000 + static_cast<std::uint64_t>(kind->respawnHours * 0.2 * 3600000.0);
        RequireGathering(harvest.depleted.front().until == until && test.gathering.IsDepleted(0, kNettle, kSource, until - 1) && !test.gathering.IsDepleted(0, kNettle, kSource, until) && !test.gathering.IsDepleted(7, kNettle, kSource, 5000),
            "the source regrows five times faster, in its own world only");
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("a2"), kBeside, 0, 60000, 6000) == "depleted", "a picked source cannot be picked again");
        RequireGathering(test.gathering.IsDepleted(0, kNettle, kSource + glm::vec3(0.01F, 0.0F, 0.0F), 6000) && test.gathering.IsDepleted(0, kNettle, kSource + glm::vec3(0.0F, -0.05F, 0.3F), 6000), "a picked source moved by a few centimetres is still picked");
        RequireGathering(!test.gathering.IsDepleted(0, kNettle, kSource + glm::vec3(0.5F, 0.0F, 0.0F), 6000), "a neighbour outside the harvest stays");
    }

    void YieldCountsVerifiedNeighbours() {
        Fixture test;
        test.level        = 30.0F;
        const auto radius = KCDC::Shared::Gathering::Radius(30.0F);
        // A neighbour; one on a slope (planar, as the game); the first again a
        // few centimetres off; one outside the server's own radius; the source.
        const std::vector<glm::vec3> reported {kSource + glm::vec3(0.5F, 0.0F, 0.0F), kSource + glm::vec3(0.0F, 0.6F, 2.0F), kSource + glm::vec3(0.52F, 0.0F, 0.0F), kSource + glm::vec3(radius + 0.5F, 0.0F, 0.0F), kSource};
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("y1"), kBeside, 0, 1000, 5000).empty(), "fixture begins");
        const auto harvest = test.gathering.Complete(kPlayer, test.Complete("y1", reported), kBeside, 0, true, 3000, 5000);
        const auto *kind   = KCDC::Shared::Game::FindGatheringKind(kNettle);
        RequireGathering(harvest.code.empty() && harvest.amount == KCDC::Shared::Gathering::CalculateYield(30.0F, 3, kind->amount), "only distinct sources within the server's radius count");
        RequireGathering(harvest.depleted.size() == 3 && test.gathering.IsDepleted(0, kNettle, reported[0], 6000) && test.gathering.IsDepleted(0, kNettle, reported[1], 6000) && !test.gathering.IsDepleted(0, kNettle, reported[3], 6000),
            "every counted source is picked, nothing else");
    }

    void YieldIsBoundedAndSkipsPicked() {
        Fixture test;
        test.level = 30.0F;
        std::vector<glm::vec3> flood;
        for (int x = -8; x <= 8; ++x) {
            for (int y = -8; y <= 8; ++y) {
                flood.push_back(kSource + glm::vec3(x * 0.15F, y * 0.15F, 0.0F));
            }
        }
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("f1"), kBeside, 0, 1000, 5000).empty(), "fixture begins");
        const auto harvest = test.gathering.Complete(kPlayer, test.Complete("f1", flood), kBeside, 0, true, 3000, 5000);
        const auto *kind   = KCDC::Shared::Game::FindGatheringKind(kNettle);
        RequireGathering(harvest.code.empty() && harvest.depleted.size() == KCDC::Features::Gathering::GatheringModule::kMaximumSources && harvest.amount == KCDC::Shared::Gathering::CalculateYield(30.0F, KCDC::Features::Gathering::GatheringModule::kMaximumSources, kind->amount),
            "a reported patch counts at most the cap");

        const glm::vec3 other = kSource + glm::vec3(0.0F, 5.0F, 0.0F);
        const glm::vec3 near  = other + glm::vec3(0.4F, 0.0F, 0.0F);
        RequireGathering(test.gathering.Begin(kPlayer, {"begin", "f2", kNettle, near, {}}, other, 0, 60000, 6000).empty() && test.gathering.Complete(kPlayer, test.Complete("f2"), other, 0, true, 62000, 6000).code.empty(), "a lone neighbour is picked first");
        RequireGathering(test.gathering.Begin(kPlayer, {"begin", "f3", kNettle, other, {}}, other, 0, 64000, 6000).empty(), "a fresh source begins");
        const auto second = test.gathering.Complete(kPlayer, test.Complete("f3", {near}), other, 0, true, 66000, 6000);
        RequireGathering(second.code.empty() && second.depleted.size() == 1 && second.amount == KCDC::Shared::Gathering::CalculateYield(30.0F, 1, kind->amount), "a neighbour already picked does not count again");
    }

    void SyncSendsOnlyNews() {
        Fixture test;
        constexpr std::uint64_t kOther = 77;
        RequireGathering(test.gathering.Sync(kOther, 0, kBeside, 1000, 5000)->empty(), "initial sync identifies an empty world");
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("s1"), kBeside, 0, 1000, 5000).empty() && test.gathering.Complete(kPlayer, test.Complete("s1"), kBeside, 0, true, 3000, 5000).code.empty(), "fixture harvests");
        const auto first = test.gathering.Sync(kOther, 0, kBeside, 20000, 6000);
        RequireGathering(first.has_value() && first->size() == 1 && first->front().kind == kNettle, "a player new to the area is told what is picked");
        RequireGathering(!test.gathering.Sync(kOther, 0, kBeside, 40000, 6000).has_value(), "the same area again sends nothing new");
        RequireGathering(test.gathering.Sync(kOther, 0, kBeside + glm::vec3(500.0F, 0.0F, 0.0F), 60000, 6000)->empty(), "a distant area reports no picked herbs");
        const auto back = test.gathering.Sync(kOther, 0, kBeside, 80000, 6000);
        RequireGathering(back.has_value() && back->size() == 1, "coming back to the area is told again");
        std::size_t answered = 0;
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (test.gathering.Sync(kOther, 0, kBeside + glm::vec3(attempt % 2 == 0 ? 500.0F : 0.0F, 0.0F, 0.0F), 90000, 6000).has_value()) {
                ++answered;
            }
        }
        RequireGathering(answered < 5, "a flood of syncs is rate limited");
    }

    void DepletionKeepsSpeciesSeparate() {
        Fixture test;
        constexpr std::uint8_t kOtherKind = 7;
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("n1"), kBeside, 0, 1000, 5000).empty() && test.gathering.Complete(kPlayer, test.Complete("n1"), kBeside, 0, true, 3000, 5000).code.empty(), "fixture picks a nettle");
        const auto neighbour = kSource + glm::vec3(0.05F, 0.0F, 0.0F);
        RequireGathering(test.gathering.IsDepleted(0, kNettle, neighbour, 6000) && !test.gathering.IsDepleted(0, kOtherKind, neighbour, 6000), "position tolerance applies only to the same species");
        RequireGathering(test.gathering.Begin(kPlayer, {"begin", "o1", kOtherKind, neighbour, {}}, kBeside, 0, 10000, 6000).empty(), "a different species beside a picked plant can begin");
        RequireGathering(test.gathering.Complete(kPlayer, test.Complete("o1"), kBeside, 0, true, 12000, 6000).code.empty(), "a different species beside a picked plant can complete");

        const auto fresh = kSource + glm::vec3(0.4F, 0.0F, 0.0F);
        RequireGathering(test.gathering.Begin(kPlayer, {"begin", "n2", kNettle, fresh, {}}, kBeside, 0, 20000, 7000).empty(), "another nettle can begin");
        // Only the other species was picked here; it must not suppress a
        // same-kind source reported by the native neighbouring-plant query.
        const auto other = kSource + glm::vec3(0.4F, 0.4F, 0.0F);
        RequireGathering(test.gathering.Begin(77, {"begin", "o2", kOtherKind, other, {}}, kBeside, 0, 20000, 7000).empty() && test.gathering.Complete(77, test.Complete("o2"), kBeside, 0, true, 22000, 7000).code.empty(), "another species is picked beside the second nettle");
        const auto harvest = test.gathering.Complete(kPlayer, test.Complete("n2", {other}), kBeside, 0, true, 23000, 7000);
        RequireGathering(harvest.code.empty() && harvest.depleted.size() == 2, "another species does not remove a source from the yield");
    }

    void JoinReplaysEarlierHarvests() {
        Fixture test;
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("j1"), kBeside, 0, 1000, 5000).empty(), "fixture begins");
        const auto harvest = test.gathering.Complete(kPlayer, test.Complete("j1"), kBeside, 0, true, 3000, 5000);
        RequireGathering(harvest.code.empty(), "fixture harvests before the other player joins");
        constexpr std::uint64_t kNewcomer = 77;
        const auto joined                 = test.gathering.Sync(kNewcomer, 0, kBeside, 10000, 6000);
        RequireGathering(joined && joined->size() == 1 && joined->front().kind == kNettle && joined->front().position == kSource && joined->front().until == harvest.depleted.front().until, "joining replays the harvested species, position and deadline");
        test.gathering.Forget(kNewcomer);
        const auto rejoined = test.gathering.Sync(kNewcomer, 0, kBeside, 20000, 6000);
        RequireGathering(rejoined && rejoined->size() == 1, "reconnecting replays old depletion even with the same player id");
        RequireGathering(test.gathering.Sync(kNewcomer, 1, kBeside, 30000, 6000)->empty(), "another virtual world explicitly receives empty state");
        RequireGathering(test.gathering.Sync(88, 0, kBeside, 40000, harvest.depleted.front().until)->empty(), "a newcomer does not receive plants that regrew");
    }

    void BeginIsChecked() {
        Fixture test;
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("b1", 250), kBeside, 0, 1000, 5000) == "unknownKind", "an unknown kind is refused");
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("b2"), kBeside + glm::vec3(10.0F, 0.0F, 0.0F), 0, 1000, 5000) == "tooFar", "a source out of reach is refused");
        std::size_t refused = 0;
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (test.gathering.Begin(kPlayer, test.Begin("r"), kBeside, 0, 2000, 5000) == "rateLimited") {
                ++refused;
            }
        }
        RequireGathering(refused > 0, "a flood of attempts is rate limited");
    }

    void CompleteIsChecked() {
        Fixture test;
        RequireGathering(test.gathering.Complete(kPlayer, test.Complete("none"), kBeside, 0, true, 3000, 5000).code == "unknownAttempt", "a completion needs its begin");
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("c1"), kBeside, 0, 1000, 5000).empty(), "fixture begins");
        RequireGathering(test.gathering.Complete(kPlayer, test.Complete("c1"), kBeside + glm::vec3(10.0F, 0.0F, 0.0F), 0, true, 3000, 5000).code == "tooFar", "walking away ends the attempt");
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("c2"), kBeside, 0, 4000, 5000).empty() && test.gathering.Complete(kPlayer, test.Complete("c2"), kBeside, 0, false, 6000, 5000).code == "cannotAct", "an incapacitated player gathers nothing");
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("c3"), kBeside, 0, 7000, 5000).empty() && test.gathering.Complete(kPlayer, test.Complete("c3"), kBeside, 0, true, 7000 + 31000, 5000).code == "expired", "an attempt left too long expires");
        test.level = std::nullopt;
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("c4"), kBeside, 0, 40000, 5000).empty() && test.gathering.Complete(kPlayer, test.Complete("c4"), kBeside, 0, true, 42000, 5000).code == "progressionUnavailable", "no survival level, no harvest");
        RequireGathering(test.inventory.Rows().empty() && !test.gathering.IsDepleted(0, kNettle, kSource, 5000), "a refused harvest grants and depletes nothing");
    }

    void VetoRefusesTheHarvest() {
        Fixture test;
        test.allow = false;
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("v1"), kBeside, 0, 1000, 5000).empty(), "fixture begins");
        RequireGathering(test.gathering.Complete(kPlayer, test.Complete("v1"), kBeside, 0, true, 3000, 5000).code == "gatheringHarvestRejected", "a script veto refuses the harvest");
        RequireGathering(test.inventory.Rows().empty() && test.xp == 0.0 && !test.gathering.IsDepleted(0, kNettle, kSource, 5000), "a vetoed harvest grants, pays and depletes nothing");
    }

    void RacingPickReplaysCommittedDepletion() {
        for (const double hours : {0.0, 1.0}) {
            Fixture test;
            RequireGathering(test.gathering.SetRespawnTime(hours, hours), "fixture selects disabled or fixed regrowth");
            const KCDC::Features::Gathering::GatheringRequest rival {"begin", "rival", kNettle, kSource + glm::vec3(0.01F, 0, 0), {}};
            RequireGathering(test.gathering.Begin(kPlayer, test.Begin("winner"), kBeside, 0, 1000, 5000).empty() && test.gathering.Begin(77, rival, kBeside, 0, 1000, 5000).empty(), "two players can start the same available herb");
            const auto winner = test.gathering.Complete(kPlayer, test.Complete("winner"), kBeside, 0, true, 3000, 5000);
            const auto loser  = test.gathering.Complete(77, test.Complete("rival"), kBeside, 0, true, 3100, 5000);
            RequireGathering(winner.code.empty() && loser.code == "depleted" && loser.outputs.empty() && loser.amount == 0 && test.xp == 5.0, "only the winner receives herbs and XP");
            RequireGathering(loser.depleted.size() == 1 && loser.depleted.front().position == kSource && loser.depleted.front().kind == kNettle && loser.depleted.front().until == winner.depleted.front().until,
                "the refusal replays the winner's actual source and deadline, including disabled regrowth");
            RequireGathering(test.gathering.Begin(77, rival, kBeside, 0, 4000, 6000) == "depleted", "later starts also receive a depleted refusal");
            const auto *replayed = test.gathering.FindDepleted(0, kNettle, rival.position, 6000);
            RequireGathering(replayed && replayed->position == kSource && replayed->until == winner.depleted.front().until, "begin refusal can replay the canonical source instead of the shifted report");
        }
    }

    void RespawnTimingCanBeChanged() {
        Fixture test;
        RequireGathering(!test.gathering.GetRespawnTime(), "species defaults have no override");
        RequireGathering(test.gathering.SetRespawnTime(1, 1), "equal bounds select a fixed duration");
        for (const auto bounds : std::vector<std::pair<double, double>> {{-1, 1}, {2, 1}, {0, 87601}, {0, 0.00000001}, {0, std::numeric_limits<double>::infinity()}, {std::numeric_limits<double>::quiet_NaN(), 1}}) {
            RequireGathering(!test.gathering.SetRespawnTime(bounds.first, bounds.second) && test.gathering.GetRespawnTime()->minHours == 1, "invalid ranges leave the policy untouched");
        }
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("t1"), kBeside, 0, 1000, 5000).empty(), "fixture begins with override");
        const auto harvest = test.gathering.Complete(kPlayer, test.Complete("t1"), kBeside, 0, true, 3000, 5000);
        RequireGathering(harvest.code.empty() && harvest.depleted.front().until == 3605000, "override is absolute game hours, without another default multiplier");
        RequireGathering(test.gathering.Sync(77, 0, kBeside, 10000, 6000).has_value(), "observer receives original deadline");
        RequireGathering(test.gathering.SetRespawnTime(0.5, 0.5), "duration can be shortened");
        const auto shortened = test.gathering.Reschedule(10000);
        RequireGathering(shortened.at(0).front().until == 1810000, "existing timers restart from now");
        const auto news = test.gathering.Sync(77, 0, kBeside, 20000, 10000);
        RequireGathering(news && news->front().until == 1810000 && news->front().sequence > harvest.depleted.front().sequence, "a stationary observer receives a revised deadline");
        RequireGathering(test.gathering.SetRespawnTime(0, 0), "zero disables timed respawn");
        RequireGathering(test.gathering.Reschedule(20000).at(0).front().until == 0, "already picked plants can be made permanent");
        test.gathering.Prune(1000000000);
        RequireGathering(test.gathering.IsDepleted(0, kNettle, kSource, 1000000000), "disabled plants survive pruning and clock advances");
        const auto joined = test.gathering.Sync(88, 0, kBeside, 30000, 1000000000);
        RequireGathering(joined && joined->front().until == 0, "late joiners receive permanent depletion");
        test.gathering.ResetRespawnTime();
        const auto resumed         = test.gathering.Reschedule(1000000000);
        const auto defaultDuration = static_cast<std::uint64_t>(KCDC::Shared::Game::FindGatheringKind(kNettle)->respawnHours * 0.2 * 3600000.0);
        RequireGathering(!test.gathering.GetRespawnTime() && resumed.at(0).front().until == 1000000000 + defaultDuration, "reset resumes disabled plants with faster species defaults from now");
        RequireGathering(test.gathering.Reschedule(resumed.at(0).front().until).empty(), "retiming never resurrects expired depletion");
    }

    void RespawnRangesAndWorlds() {
        Fixture test;
        test.level = 30.0F;
        RequireGathering(test.gathering.SetRespawnTime(1, 2), "random range is accepted");
        const std::vector<glm::vec3> neighbours {kSource + glm::vec3(0.5F, 0, 0), kSource + glm::vec3(0, 0.5F, 0)};
        for (const std::uint32_t world : {0U, 7U}) {
            RequireGathering(test.gathering.Begin(kPlayer, test.Begin("r1"), kBeside, world, 1000 + world * 10000, 5000).empty(), "same positions can be picked in independent worlds");
            const auto harvest = test.gathering.Complete(kPlayer, test.Complete("r1", neighbours), kBeside, world, true, 3000 + world * 10000, 5000);
            RequireGathering(harvest.code.empty() && harvest.depleted.size() == 3, "three sources were picked");
            for (const auto &point : harvest.depleted) {
                RequireGathering(point.until >= 3605000 && point.until <= 7205000, "each deadline falls within the configured range");
            }
        }
        const auto restarted = test.gathering.Reschedule(10000);
        RequireGathering(restarted.size() == 2, "retiming covers all virtual worlds");
        for (const auto &[world, points] : restarted) {
            for (const auto &point : points) {
                RequireGathering(point.until >= 3610000 && point.until <= 7210000, "resampled deadlines start at the new world time");
            }
        }
    }

    void ManualRespawnUsesASphere() {
        Fixture test;
        test.level = 30.0F;
        RequireGathering(test.gathering.SetRespawnTime(0, 0), "fixture disables timed respawn");
        const auto edge  = kSource + glm::vec3(0.5F, 0, 0);
        const auto above = kSource + glm::vec3(0, 0.5F, 2);
        for (const std::uint32_t world : {0U, 7U}) {
            RequireGathering(test.gathering.Begin(kPlayer, test.Begin("m1"), kBeside, world, 1000 + world * 10000, 5000).empty(), "fixture begins");
            const auto harvest = test.gathering.Complete(kPlayer, test.Complete("m1", {edge, above}), kBeside, world, true, 3000 + world * 10000, 5000);
            RequireGathering(harvest.code.empty() && harvest.depleted.size() == 3 && harvest.depleted.front().until == 0, "future harvests also honour disabled timers");
        }
        RequireGathering(test.gathering.Respawn(0, kSource, -1, 6000).empty(), "a negative radius changes nothing");
        RequireGathering(test.gathering.Respawn(0, kSource, std::numeric_limits<float>::quiet_NaN(), 6000).empty(), "a NaN radius changes nothing");
        RequireGathering(test.gathering.Respawn(0, kSource, 0.5F, 6000).size() == 2, "sphere includes its boundary");
        RequireGathering(!test.gathering.IsDepleted(0, kNettle, edge, 6000) && test.gathering.IsDepleted(0, kNettle, above, 6000) && test.gathering.IsDepleted(7, kNettle, kSource, 6000), "manual respawn respects height and virtual world");
        RequireGathering(test.gathering.Respawn(0, kSource, 0.5F, 6000).empty(), "repeating a manual respawn returns zero");
        RequireGathering(test.gathering.Begin(kPlayer, test.Begin("m2"), kBeside, 0, 100000, 6000).empty() && test.gathering.Complete(kPlayer, test.Complete("m2"), kBeside, 0, true, 102000, 6000).code.empty(), "manually restored herbs can be harvested again");
        RequireGathering(test.gathering.Respawn(0, std::nullopt, 0, 7000).size() == 2 && test.gathering.Respawn(7, std::nullopt, 0, 7000).size() == 3, "respawn-all restores only the chosen world's remaining records");
        RequireGathering(test.gathering.Sync(99, 0, kBeside, 200000, 7000)->empty(), "newcomers receive no stale depletion after manual respawn");
    }

    void RequestsAreRead() {
        KCDC::Features::Gathering::GatheringRequest request;
        RequireGathering(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"begin","id":"a1","kind":2,"position":[1,2,3]})", request) && request.action == "begin" && request.kind == 2 && request.position == glm::vec3(1.0F, 2.0F, 3.0F),
            "a begin names its source");
        RequireGathering(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"complete","id":"a1"})", request) && request.action == "complete" && request.sources.empty(), "a completion names its attempt");
        RequireGathering(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"complete","id":"a1","sources":[[1,2,3],[4,5,6]]})", request) && request.sources.size() == 2 && request.sources[1] == glm::vec3(4.0F, 5.0F, 6.0F),
            "a completion may name the sources the game would harvest");
        RequireGathering(!KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"complete","id":"a1","sources":3})", request), "a source count is not a source list");
        RequireGathering(!KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"complete","id":"a1","sources":[[1,2]]})", request), "a malformed source is refused");
        std::string many = R"({"action":"complete","id":"a1","sources":[)";
        for (int index = 0; index < 200; ++index) {
            many += std::string(index == 0 ? "" : ",") + "[1,2,3]";
        }
        RequireGathering(!KCDC::Features::Gathering::GatheringModule::ReadRequest(many + "]}", request), "an oversized source list is refused");
        RequireGathering(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"sync"})", request) && request.action == "sync", "a sync asks for nearby depletion");
        RequireGathering(!KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"begin","id":"a1","kind":2,"position":[1,2]})", request), "a malformed position is refused");
        RequireGathering(!KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"begin","id":"a1","kind":2,"position":[1,2,"x"]})", request), "a non-numeric position is refused");
        RequireGathering(!KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"explode"})", request), "an unknown action is refused");
    }
} // namespace

void GatheringTests() {
    RulesMatchTheGame();
    HarvestGrantsAndDepletes();
    YieldCountsVerifiedNeighbours();
    YieldIsBoundedAndSkipsPicked();
    SyncSendsOnlyNews();
    DepletionKeepsSpeciesSeparate();
    JoinReplaysEarlierHarvests();
    BeginIsChecked();
    CompleteIsChecked();
    VetoRefusesTheHarvest();
    RacingPickReplaysCommittedDepletion();
    RespawnTimingCanBeChanged();
    RespawnRangesAndWorlds();
    ManualRespawnUsesASphere();
    RequestsAreRead();
}
