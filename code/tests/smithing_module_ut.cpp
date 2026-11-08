#include "features/crafting/smithing_evaluator.h"
#include "features/crafting/smithing_module.h"
#include "features/inventory/inventory.h"
#include "shared/features/crafting/smithing_native_ids.h"
#include "shared/game/buff_rule_catalog.h"
#include "shared/game/crafting_catalog.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/inventory_property_policy.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace {
    constexpr std::uint64_t kPlayer = 7;
    constexpr std::uint64_t kRival  = 8;

    void RequireSmithing(bool condition, const std::string &message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    /** The perk whose own buff is `buff`. */
    std::string PerkGranting(std::string_view buff) {
        const auto edge = std::find_if(KCDC::Shared::Game::g_PerkBuffRules.begin(), KCDC::Shared::Game::g_PerkBuffRules.end(), [buff](const KCDC::Shared::Game::PerkBuffRule &rule) {
            return rule.buff == buff;
        });
        RequireSmithing(edge != KCDC::Shared::Game::g_PerkBuffRules.end(), "fixture perk for buff " + std::string(buff));
        return std::string(edge->perk);
    }

    const KCDC::Shared::Game::SmithingMaterialRule &Rule(std::string_view item) {
        const auto *rule = KCDC::Shared::Game::FindSmithingMaterialRule(item);
        RequireSmithing(rule != nullptr, "material rule for " + std::string(item));
        return *rule;
    }

    /**
     * An untagged recipe making one mutable, non-quest weapon from a divisible
     * material of at least two units and a material that is not divisible.
     */
    const KCDC::Shared::Game::CraftRecipe &WeaponRecipe() {
        for (const auto &recipe : KCDC::Shared::Game::g_CraftRecipes) {
            if (recipe.kind != KCDC::Shared::Game::CraftKind::Smithing || recipe.dlc != 0 || recipe.products.size() != 1) {
                continue;
            }
            const auto *product    = KCDC::Shared::Game::FindSmithingProductClass(recipe.products.front().item);
            const auto *properties = KCDC::Shared::Game::FindInventoryPropertyClass(recipe.products.front().item);
            if (product == nullptr || properties == nullptr || product->kind != KCDC::Shared::Game::SmithingProductKind::MeleeWeapon || product->questItem || !properties->canHealthChange || properties->maximumQuality < 4
                || product->weaponClass == KCDC::Shared::Crafting::SmithingNative::kConditionExemptWeaponClass) {
                continue;
            }
            const bool divisible = std::any_of(recipe.ingredients.begin(), recipe.ingredients.end(), [](const KCDC::Shared::Game::CraftMaterial &material) {
                return material.amount >= 2 && Rule(material.item).divisible && !Rule(material.item).questItem;
            });
            const bool solid     = std::any_of(recipe.ingredients.begin(), recipe.ingredients.end(), [](const KCDC::Shared::Game::CraftMaterial &material) {
                return !Rule(material.item).divisible && !Rule(material.item).questItem;
            });
            if (divisible && solid) {
                return recipe;
            }
        }
        throw std::runtime_error("no fixture weapon recipe");
    }

    const KCDC::Shared::Game::CraftRecipe &QuestMaterialRecipe() {
        for (const auto &recipe : KCDC::Shared::Game::g_CraftRecipes) {
            if (recipe.kind == KCDC::Shared::Game::CraftKind::Smithing && recipe.dlc == 0 && std::any_of(recipe.ingredients.begin(), recipe.ingredients.end(), [](const KCDC::Shared::Game::CraftMaterial &material) {
                    return Rule(material.item).questItem;
                })) {
                return recipe;
            }
        }
        throw std::runtime_error("no fixture quest-material recipe");
    }

    KCDC::Features::Crafting::SmithingCharacterContext Character(const KCDC::Shared::Game::CraftRecipe &recipe) {
        KCDC::Features::Crafting::SmithingCharacterContext character;
        character.skill = 20.0F;
        character.perks.insert(std::string(recipe.perk));
        return character;
    }

    /** One player at the first authored smithery, holding the fixture recipe's materials twice over. */
    struct SmithingFixture {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory {ids};
        KCDC::Features::Inventory::Inventory rivalInventory {ids};
        KCDC::Features::Crafting::SmithingModule smithing;
        KCDC::Features::Crafting::SmithingActor actor;
        const KCDC::Shared::Game::CraftRecipe &recipe;
        std::string station;
        std::vector<double> xp;
        std::vector<nlohmann::json> starting;
        std::vector<nlohmann::json> completing;
        std::map<std::string, std::uint32_t> worn;
        bool alchemy               = false;
        std::vector<nlohmann::json> refunds;
        bool refundInterruptions   = false;
        bool reenterRefund         = false;
        bool throwRefund           = false;
        bool throwInventory        = false;
        bool allowStart            = true;
        bool allowComplete         = true;
        std::uint32_t coin         = 0;
        std::uint32_t lockpickDraw = 0;

        explicit SmithingFixture(const KCDC::Shared::Game::CraftRecipe &fixtureRecipe = WeaponRecipe()): recipe(fixtureRecipe) {
            KCDC::Features::Crafting::SmithingHooks hooks;
            hooks.inventory = [this](std::uint64_t playerId) {
                if (throwInventory) {
                    throw std::runtime_error("inventory unavailable");
                }
                return playerId == kPlayer ? &inventory : playerId == kRival ? &rivalInventory : nullptr;
            };
            hooks.starting = [this](std::uint64_t, const nlohmann::json &proposal) {
                starting.push_back(proposal);
                return allowStart;
            };
            hooks.completing = [this](std::uint64_t, const nlohmann::json &proposal) {
                completing.push_back(proposal);
                return allowComplete;
            };
            hooks.refunding = [this](std::uint64_t playerId, const nlohmann::json &proposal) {
                refunds.push_back(proposal);
                if (reenterRefund) {
                    RequireSmithing(!smithing.End(playerId, "cancelled") && smithing.Begin(actor, station, std::string(recipe.id)).code == KCDC::Shared::RPC::SmithingCode::PlayerBusy, "settlement cannot be reentered");
                }
                if (throwRefund) {
                    throw std::runtime_error("refund policy failed");
                }
                return refundInterruptions && proposal.at("reason") == "interrupted";
            };
            hooks.experience = [this](std::uint64_t, double amount) {
                xp.push_back(amount);
            };
            hooks.equipped = [this](std::uint64_t) {
                return &worn;
            };
            hooks.busy = [this](std::uint64_t) {
                return alchemy;
            };
            hooks.random = [this](std::uint32_t bound) {
                return bound == 2 ? coin : lockpickDraw;
            };
            smithing           = KCDC::Features::Crafting::SmithingModule(std::move(hooks));
            const auto *row    = &*std::find_if(KCDC::Shared::Game::g_CraftStations.begin(), KCDC::Shared::Game::g_CraftStations.end(), [](const auto &entry) {
                return entry.kind == KCDC::Shared::Game::CraftKind::Smithing;
            });
            station            = std::string(row->id);
            actor.playerId     = kPlayer;
            actor.nowMs        = 100000;
            actor.position     = {row->x, row->y, row->z};
            actor.virtualWorld = 2;
            actor.level        = std::string(row->level);
            actor.canAct       = true;
            actor.character    = Character(recipe);
            for (const auto &material : recipe.ingredients) {
                const auto added = inventory.Add(std::string(material.item), material.amount * 2, nlohmann::json::object(), std::nullopt);
                // Only its quest hands out a quest item; nothing can stock one.
                if (Rule(material.item).questItem) {
                    RequireSmithing(!added.ok && added.code == "questItem", "a quest material cannot be stocked");
                }
                else {
                    RequireSmithing(added.ok, "fixture stock: " + added.code);
                }
            }
            inventory.TakeChanges();
        }

        std::uint32_t Held(std::string_view item) const {
            std::uint32_t amount = 0;
            for (const auto &[id, row] : inventory.Rows()) {
                if (row.itemClass == item) {
                    amount += row.amount;
                }
            }
            return amount;
        }

        std::uint32_t Begin() {
            const auto reply = smithing.Begin(actor, station, std::string(recipe.id));
            RequireSmithing(reply.code == KCDC::Shared::RPC::SmithingCode::Accepted && reply.session != 0, "workpiece begins: " + std::to_string(static_cast<int>(reply.code)));
            return reply.session;
        }

        std::vector<std::string> EventNames() {
            std::vector<std::string> names;
            for (const auto &event : smithing.TakeEvents()) {
                names.push_back(event.name);
            }
            return names;
        }
    };

    void StationToolTests() {
        // Every smithery links a tongs and a hammer slot; the client keeps
        // those spawning so the minigame has tools to hand the smith.
        RequireSmithing(KCDC::Shared::Game::g_CraftStationTools.size() == 56, "two tool slots for each of the 28 smitheries");
        const auto smithing = std::count_if(KCDC::Shared::Game::g_CraftStations.begin(), KCDC::Shared::Game::g_CraftStations.end(), [](const KCDC::Shared::Game::CraftStation &station) {
            return station.kind == KCDC::Shared::Game::CraftKind::Smithing;
        });
        RequireSmithing(smithing == 28, "28 smitheries compiled");
        bool tongs = false, hammer = false;
        for (const auto &tool : KCDC::Shared::Game::g_CraftStationTools) {
            RequireSmithing(KCDC::Shared::Game::FindCraftStation(tool.station) != nullptr && KCDC::Shared::Game::FindCraftStation(tool.station)->kind == KCDC::Shared::Game::CraftKind::Smithing, "a tool slot belongs to a smithery");
            tongs |= tool.station == "kutnohorsko:400d8d35f0b6be16" && tool.slotGuid == 0x428968aaa19121bcULL;
            hammer |= tool.station == "kutnohorsko:400d8d35f0b6be16" && tool.slotGuid == 0x4798adf2801ba5f6ULL;
        }
        RequireSmithing(tongs && hammer, "the Suchdol smithery's TongsSlot and HammerSlot are catalogued");
    }

    void EvaluatorTests() {
        KCDC::Features::Player::SoulSnapshot soul;
        soul.clock                  = {1000, 15000};
        soul.profile.health         = 100.0F;
        soul.profile.skillLevels[8] = 10;
        KCDC::Features::Crafting::SmithingCharacterContext plain;
        std::string error;
        RequireSmithing(KCDC::Features::Crafting::SmithingEvaluator::Evaluate(soul, plain, error) && plain.skill == 10.0F && plain.maximumLockpicks == 0 && !plain.superiorQuality && !plain.hardenedSteel, "a plain smith reads the skill level and no byproduct");

        // Swordmaker's Wisdom counts during blacksmithing; the locksmith perk authors lfb+3.
        soul.profile.perks.insert(PerkGranting("4c31611c-4b46-4edc-a5f6-079adb97b460"));
        soul.profile.perks.insert(PerkGranting("6da0b521-6182-4ecb-be5f-73a99e634138"));
        KCDC::Features::Crafting::SmithingCharacterContext perked;
        RequireSmithing(KCDC::Features::Crafting::SmithingEvaluator::Evaluate(soul, perked, error) && perked.skill == 13.0F && perked.maximumLockpicks == 3, "perks raise the skill during smithing and allow three lockpicks");

        const auto &recipe = WeaponRecipe();
        auto character     = Character(recipe);
        RequireSmithing(!KCDC::Features::Crafting::SmithingEvaluator::Admits(recipe, character), "the recipe perk and skill admit the recipe");
        character.perks.clear();
        RequireSmithing(KCDC::Features::Crafting::SmithingEvaluator::Admits(recipe, character) == std::optional<std::string>("recipeNotKnown"), "without the recipe perk the recipe is unknown");
        character       = Character(recipe);
        character.skill = static_cast<float>(recipe.minSkill) - 1.0F;
        RequireSmithing(KCDC::Features::Crafting::SmithingEvaluator::Admits(recipe, character) == std::optional<std::string>("skillTooLow"), "below the minimum skill the recipe is refused");

        character         = Character(recipe);
        const float floor = KCDC::Features::Crafting::SmithingEvaluator::CriticalQuality(recipe);
        RequireSmithing(!KCDC::Features::Crafting::SmithingEvaluator::Product(recipe, floor - 0.02F, character), "below the lowest threshold nothing is made");
        RequireSmithing(!KCDC::Features::Crafting::SmithingEvaluator::Product(recipe, 1.5F, character) && !KCDC::Features::Crafting::SmithingEvaluator::Product(recipe, std::nanf(""), character), "an impossible quality makes nothing");
        const auto best = KCDC::Features::Crafting::SmithingEvaluator::Product(recipe, 1.0F, character);
        RequireSmithing(best && best->itemClass == recipe.products.front().item && !best->questItem, "a perfect workpiece makes the recipe's product");
        RequireSmithing(best->metadata.at("quality") == 3U, "without ability 65 the tier stops at three");
        RequireSmithing(best->xp == 135.0F, "a perfect workpiece earns the authored finishing XP");
        const float condition = best->metadata.value("condition", 1.0F);
        RequireSmithing(condition >= 0.1F && condition <= 0.8F + 1e-6F, "condition lies in the authored range");

        // A recipe whose only product is a quest item still succeeds: it is
        // graded, and nothing is created for the inventory to refuse.
        const auto questRecipe = std::find_if(KCDC::Shared::Game::g_CraftRecipes.begin(), KCDC::Shared::Game::g_CraftRecipes.end(), [](const KCDC::Shared::Game::CraftRecipe &candidate) {
            if (candidate.kind != KCDC::Shared::Game::CraftKind::Smithing || candidate.products.size() != 1) {
                return false;
            }
            const auto *product = KCDC::Shared::Game::FindSmithingProductClass(candidate.products.front().item);
            return product != nullptr && product->questItem;
        });
        RequireSmithing(questRecipe != KCDC::Shared::Game::g_CraftRecipes.end(), "a fixture recipe makes a quest product");
        const auto questProduct = KCDC::Features::Crafting::SmithingEvaluator::Product(*questRecipe, 1.0F, Character(*questRecipe));
        RequireSmithing(questProduct && questProduct->questItem && questProduct->metadata.contains("quality") && !questProduct->metadata.contains("condition"), "a quest product is graded but carries no item properties");
        RequireSmithing(!best->metadata.contains("onEquipBuffs"), "no Hardened Steel without its ability");

        character.superiorQuality = true;
        character.hardenedSteel   = true;
        const auto superior       = KCDC::Features::Crafting::SmithingEvaluator::Product(recipe, 1.0F, character);
        RequireSmithing(superior && superior->metadata.at("quality") == 4U, "ability 65 reaches the fourth tier");
        RequireSmithing(superior->metadata.at("onEquipBuffs") == nlohmann::json::array({KCDC::Shared::Inventory::kHardenedSteelItemBuff}), "Hardened Steel rides on a smithed weapon");
        const auto rough = KCDC::Features::Crafting::SmithingEvaluator::Product(recipe, floor, character);
        RequireSmithing(rough && rough->metadata.at("quality") == 1U && rough->xp < best->xp, "a workpiece at the threshold makes the lowest tier for less XP");

        const KCDC::Shared::Game::SmithingMaterialRule divisible {"x", false, true};
        const KCDC::Shared::Game::SmithingMaterialRule solid {"x", false, false};
        const KCDC::Shared::Game::SmithingMaterialRule quest {"x", true, true};
        const auto heads = []() {
            return true;
        };
        const auto tails = []() {
            return false;
        };
        RequireSmithing(KCDC::Features::Crafting::SmithingEvaluator::FailureLoss(divisible, 4, tails) == 2 && KCDC::Features::Crafting::SmithingEvaluator::FailureLoss(divisible, 3, tails) == 1, "a failure spends half of a divisible material, rounded down");
        RequireSmithing(KCDC::Features::Crafting::SmithingEvaluator::FailureLoss(divisible, 1, heads) == 1 && KCDC::Features::Crafting::SmithingEvaluator::FailureLoss(divisible, 1, tails) == 0, "a single unit is the coin's");
        RequireSmithing(KCDC::Features::Crafting::SmithingEvaluator::FailureLoss(solid, 5, heads) == 0 && KCDC::Features::Crafting::SmithingEvaluator::FailureLoss(quest, 5, heads) == 0, "an indivisible or quest material is never spent by a failure");
    }

    void AdmissionTests() {
        SmithingFixture test;
        const std::string recipe(test.recipe.id);
        auto actor      = test.actor;
        actor.character = std::nullopt;
        RequireSmithing(test.smithing.Begin(actor, test.station, recipe).code == KCDC::Shared::RPC::SmithingCode::CharacterUnavailable, "no character, no workpiece");
        actor = test.actor;
        actor.position += glm::vec3(20.0F, 0.0F, 0.0F);
        RequireSmithing(test.smithing.Begin(actor, test.station, recipe).code == KCDC::Shared::RPC::SmithingCode::NotAuthorized, "the smithery must be within reach");
        RequireSmithing(test.smithing.Begin(test.actor, "nowhere", recipe).code == KCDC::Shared::RPC::SmithingCode::UnknownStation, "an unknown station is refused");
        RequireSmithing(test.smithing.Begin(test.actor, test.station, "r_nothing").code == KCDC::Shared::RPC::SmithingCode::UnknownRecipe, "an unknown recipe is refused");
        actor = test.actor;
        actor.character->perks.clear();
        RequireSmithing(test.smithing.Begin(actor, test.station, recipe).code == KCDC::Shared::RPC::SmithingCode::RecipeNotKnown, "an unlearned recipe is refused");
        test.allowStart = false;
        RequireSmithing(test.smithing.Begin(test.actor, test.station, recipe).code == KCDC::Shared::RPC::SmithingCode::StartingRejected && test.starting.back().at("kind") == "smithing" && test.starting.back().at("recipe") == recipe,
            "craftingStarting sees the recipe and can refuse it");
        test.allowStart = true;
        RequireSmithing(test.EventNames().empty() && test.inventory.TakeChanges().empty(), "a refusal takes nothing and raises nothing");

        const auto before  = test.Held(test.recipe.ingredients.front().item);
        const auto session = test.Begin();
        RequireSmithing(test.Held(test.recipe.ingredients.front().item) == before - test.recipe.ingredients.front().amount, "beginning takes the materials");
        RequireSmithing(test.EventNames() == std::vector<std::string> {"craftingStarted"}, "craftingStarted follows the take");
        RequireSmithing(test.smithing.Begin(test.actor, test.station, recipe).code == KCDC::Shared::RPC::SmithingCode::PlayerBusy, "one workpiece per player");
        auto rival     = test.actor;
        rival.playerId = kRival;
        RequireSmithing(test.smithing.Begin(rival, test.station, recipe).code == KCDC::Shared::RPC::SmithingCode::StationBusy, "one workpiece per smithery");
        rival.virtualWorld = 9;
        RequireSmithing(test.smithing.Begin(rival, test.station, recipe).code == KCDC::Shared::RPC::SmithingCode::MissingMaterials, "another world has its own smithery, but the rival has no materials");
        RequireSmithing(test.smithing.Occupancy(test.station, test.actor.virtualWorld, test.actor.nowMs).at("occupant").at("playerId") == kPlayer && test.smithing.Inspect(kPlayer, test.actor.nowMs).at("recipe") == recipe, "the station and the session name the workpiece");
        RequireSmithing(test.smithing.Finish(test.actor, session + 1, KCDC::Shared::RPC::SmithingOutcome::Success, 1.0F).code == KCDC::Shared::RPC::SmithingCode::UnknownSession, "another session cannot be finished");
    }

    void SuccessTests() {
        SmithingFixture test;
        test.actor.character->maximumLockpicks = 3;
        test.lockpickDraw                      = 1;
        const auto session                     = test.Begin();
        test.EventNames();
        test.inventory.TakeChanges();
        const auto reply = test.smithing.Finish(test.actor, session, KCDC::Shared::RPC::SmithingOutcome::Success, 1.0F);
        RequireSmithing(reply.code == KCDC::Shared::RPC::SmithingCode::Accepted, "a finished workpiece is accepted");
        RequireSmithing(test.Held(test.recipe.products.front().item) == 1 && test.Held(KCDC::Shared::Crafting::SmithingNative::kLockpick) == 2, "the product and the drawn lockpicks are granted");
        RequireSmithing(test.xp == std::vector<double> {135.0}, "the finishing XP is ordered");
        RequireSmithing(test.completing.size() == 1 && test.completing.front().at("product") == std::string(test.recipe.products.front().item) && test.completing.front().at("lockpicks") == 2U, "craftingCompleting sees the result");
        for (const auto &material : test.recipe.ingredients) {
            RequireSmithing(test.Held(material.item) == material.amount, "a success spends every material once");
        }
        RequireSmithing(test.EventNames() == std::vector<std::string> {"craftingCompleted", "craftingEnded"}, "completed, then ended");
        RequireSmithing(!test.smithing.SessionOf(kPlayer) && test.smithing.Occupancy(test.station, test.actor.virtualWorld, test.actor.nowMs).at("occupied") == false, "the smithery is free again");
        RequireSmithing(test.smithing.Finish(test.actor, session, KCDC::Shared::RPC::SmithingOutcome::Success, 1.0F).code == KCDC::Shared::RPC::SmithingCode::UnknownSession, "a workpiece settles once");
    }

    void FailureTests() {
        SmithingFixture test;
        test.coin          = 1;
        const auto session = test.Begin();
        RequireSmithing(test.smithing.Finish(test.actor, session, KCDC::Shared::RPC::SmithingOutcome::Failure, 0.9F).code == KCDC::Shared::RPC::SmithingCode::Accepted, "a broken workpiece is accepted");
        for (const auto &material : test.recipe.ingredients) {
            const auto &rule   = Rule(material.item);
            std::uint32_t lost = 0;
            if (rule.divisible && !rule.questItem) {
                lost = std::max<std::uint32_t>(static_cast<std::uint32_t>(static_cast<float>(material.amount) * 0.5F), 1);
            }
            RequireSmithing(test.Held(material.item) == material.amount * 2 - lost, "a failure spends the native share of " + std::string(material.item));
        }
        RequireSmithing(test.Held(test.recipe.products.front().item) == 0 && test.xp.empty(), "a failure grants nothing");
        const auto events = test.smithing.TakeEvents();
        RequireSmithing(events.size() == 2 && events.back().name == "craftingEnded" && events.back().payload.at("outcome") == "failed" && !events.back().payload.at("refunded").empty(), "craftingEnded names the refund");

        SmithingFixture veto;
        veto.allowComplete = false;
        const auto vetoed  = veto.Begin();
        RequireSmithing(veto.smithing.Finish(veto.actor, vetoed, KCDC::Shared::RPC::SmithingOutcome::Success, 1.0F).code == KCDC::Shared::RPC::SmithingCode::CompletingRejected && veto.Held(veto.recipe.products.front().item) == 0 && veto.xp.empty(),
            "a refused result fails the workpiece");

        SmithingFixture low;
        const auto poor = low.Begin();
        const auto code = low.smithing.Finish(low.actor, poor, KCDC::Shared::RPC::SmithingOutcome::Success, KCDC::Features::Crafting::SmithingEvaluator::CriticalQuality(low.recipe) - 0.05F).code;
        RequireSmithing(code == KCDC::Shared::RPC::SmithingCode::InvalidQuality && low.Held(low.recipe.products.front().item) == 0, "a success below the threshold is a failure");
    }

    void ContextTests() {
        SmithingFixture test;
        // Every unit of the first material worn: nothing spare to spend.
        const auto &first = test.recipe.ingredients.front();
        for (const auto &[id, row] : test.inventory.Rows()) {
            if (row.itemClass == first.item) {
                test.worn[id] = row.amount;
            }
        }
        RequireSmithing(test.smithing.Begin(test.actor, test.station, std::string(test.recipe.id)).code == KCDC::Shared::RPC::SmithingCode::MissingMaterials, "a worn unit is never taken");
        test.worn.clear();
        test.alchemy = true;
        RequireSmithing(test.smithing.Begin(test.actor, test.station, std::string(test.recipe.id)).code == KCDC::Shared::RPC::SmithingCode::PlayerBusy, "a player at an alchemy table cannot smith");
        test.alchemy = false;

        const auto session = test.Begin();
        auto away          = test.actor;
        away.position += glm::vec3(30.0F, 0.0F, 0.0F);
        RequireSmithing(test.smithing.Finish(away, session, KCDC::Shared::RPC::SmithingOutcome::Success, 1.0F).code == KCDC::Shared::RPC::SmithingCode::NotAuthorized, "a success reported from out of reach is refused");
        RequireSmithing(test.Held(test.recipe.products.front().item) == 0 && !test.smithing.SessionOf(kPlayer), "and fails the workpiece");
    }

    void AbandonTests() {
        SmithingFixture test;
        auto session = test.Begin();
        RequireSmithing(test.smithing.Finish(test.actor, session, KCDC::Shared::RPC::SmithingOutcome::Abandoned, 0.0F).code == KCDC::Shared::RPC::SmithingCode::Accepted, "a prompt abandon is accepted");
        for (const auto &material : test.recipe.ingredients) {
            RequireSmithing(test.Held(material.item) == material.amount * 2, "an abandoned workpiece returns everything");
        }

        session = test.Begin();
        test.actor.nowMs += KCDC::Features::Crafting::SmithingModule::kAbandonWindowMs + 1;
        RequireSmithing(test.smithing.Finish(test.actor, session, KCDC::Shared::RPC::SmithingOutcome::Abandoned, 0.0F).code == KCDC::Shared::RPC::SmithingCode::AbandonTooLate, "a late abandon fails the workpiece");
    }

    void RefundTests() {
        for (const auto outcome : {KCDC::Shared::RPC::SmithingOutcome::Interrupted, KCDC::Shared::RPC::SmithingOutcome::Cancelled, KCDC::Shared::RPC::SmithingOutcome::Failure}) {
            SmithingFixture test;
            test.refundInterruptions = true;
            test.reenterRefund       = true;
            test.coin                = 1;
            const auto before        = test.inventory.Rows();
            const auto session       = test.Begin();
            RequireSmithing(test.smithing.Finish(test.actor, session, outcome, 0).code == KCDC::Shared::RPC::SmithingCode::Accepted, "each exit settles");
            RequireSmithing(test.refunds.size() == 1 && test.refunds[0].at("materials").size() == test.recipe.ingredients.size(), "one policy call carries the held materials");
            const auto reason = outcome == KCDC::Shared::RPC::SmithingOutcome::Interrupted ? "interrupted" : outcome == KCDC::Shared::RPC::SmithingOutcome::Cancelled ? "cancelled" : "craftingFailed";
            RequireSmithing(test.refunds[0].at("reason") == reason, "the policy receives the exact reason");
            RequireSmithing((test.inventory.Rows() == before) == (outcome == KCDC::Shared::RPC::SmithingOutcome::Interrupted), "only interrupted work gets a full refund");
            const auto settled = test.inventory.Rows();
            RequireSmithing(test.smithing.Finish(test.actor, session, outcome, 0).code == KCDC::Shared::RPC::SmithingCode::UnknownSession && test.inventory.Rows() == settled && test.refunds.size() == 1, "duplicate finish cannot refund twice");
            const auto events = test.smithing.TakeEvents();
            RequireSmithing(events.back().payload.at("reason") == reason && !events.back().payload.at("refunded").empty() && test.xp.empty(), "the terminal records the actual refund without granting XP");
        }
        for (const auto *reason : {"contextInvalidated", "timeout"}) {
            SmithingFixture closed;
            closed.refundInterruptions = true;
            closed.coin                = 1;
            const auto before          = closed.inventory.Rows();
            const auto session         = closed.Begin();
            if (std::string(reason) == "timeout") {
                closed.actor.nowMs += 30 * 60 * 1000;
            }
            else {
                closed.actor.canAct = false;
            }
            const auto expected = std::string(reason) == "timeout" ? KCDC::Shared::RPC::SmithingCode::SessionExpired : KCDC::Shared::RPC::SmithingCode::NotAuthorized;
            RequireSmithing(closed.smithing.Finish(closed.actor, session, KCDC::Shared::RPC::SmithingOutcome::Interrupted, 0).code == expected, "server closure overrides a client interruption");
            RequireSmithing(closed.refunds.size() == 1 && closed.refunds[0].at("reason") == reason && closed.inventory.Rows() != before, "server closure keeps the native material loss");
            RequireSmithing(!closed.smithing.SessionOf(kPlayer), "server closure releases the workpiece");
        }
        SmithingFixture fault;
        fault.throwRefund = true;
        fault.coin        = 1;
        const auto before = fault.inventory.Rows();
        fault.smithing.Finish(fault.actor, fault.Begin(), KCDC::Shared::RPC::SmithingOutcome::Interrupted, 0);
        RequireSmithing(fault.inventory.Rows() != before && !fault.smithing.SessionOf(kPlayer), "a throwing policy keeps native losses and releases the session");

        SmithingFixture broken;
        const auto brokenSession = broken.Begin();
        broken.throwInventory    = true;
        bool thrown              = false;
        try {
            broken.smithing.Finish(broken.actor, brokenSession, KCDC::Shared::RPC::SmithingOutcome::Failure, 0);
        }
        catch (const std::runtime_error &) {
            thrown = true;
        }
        broken.throwInventory = false;
        RequireSmithing(thrown && broken.smithing.Begin(broken.actor, broken.station, std::string(broken.recipe.id)).code != KCDC::Shared::RPC::SmithingCode::PlayerBusy, "a settlement that throws does not leave the player busy");

        SmithingFixture completed;
        completed.refundInterruptions = true;
        completed.smithing.Finish(completed.actor, completed.Begin(), KCDC::Shared::RPC::SmithingOutcome::Success, 0.9F);
        RequireSmithing(completed.refunds.empty(), "a granted product has no refund decision");

        for (const auto outcome : {KCDC::Shared::RPC::SmithingOutcome::Cancelled, KCDC::Shared::RPC::SmithingOutcome::Interrupted}) {
            KCDC::Shared::RPC::SmithingFinish report;
            report.session = 1;
            report.outcome = static_cast<std::uint8_t>(outcome);
            MafiaNet::BitStream wire;
            report.Serialize(&wire, true);
            wire.ResetReadPointer();
            KCDC::Shared::RPC::SmithingFinish decoded;
            decoded.Serialize(&wire, false);
            RequireSmithing(decoded.IsSane() && decoded.outcome == report.outcome, "new exit outcomes round trip");
            decoded.outcome = 255;
            RequireSmithing(!decoded.IsSane(), "unknown outcomes are refused");
        }
    }

    void LifecycleTests() {
        SmithingFixture test;
        const auto session = test.Begin();
        auto moved         = test.actor;
        moved.position += glm::vec3(30.0F, 0.0F, 0.0F);
        const auto closed = test.smithing.Expire(moved.nowMs, [&moved](std::uint64_t) {
            return std::optional<KCDC::Features::Crafting::SmithingActor>(moved);
        });
        RequireSmithing(closed.size() == 1 && closed.front().session == session && closed.front().reason == KCDC::Shared::RPC::SmithingCloseReason::ContextInvalidated, "walking away closes the workpiece");
        RequireSmithing(!test.smithing.SessionOf(kPlayer), "and releases the smithery");

        test.Begin();
        const auto expired = test.smithing.Expire(test.actor.nowMs + KCDC::Features::Crafting::SmithingModule::kSessionLifetimeMs, [&test](std::uint64_t) {
            return std::optional<KCDC::Features::Crafting::SmithingActor>(test.actor);
        });
        RequireSmithing(expired.size() == 1 && expired.front().reason == KCDC::Shared::RPC::SmithingCloseReason::Timeout, "a workpiece times out");

        test.Begin();
        RequireSmithing(test.smithing.End(kPlayer, "disconnected") && !test.smithing.End(kPlayer, "disconnected"), "a disconnect ends the workpiece once");

        // A quest material is required, and no inventory can hold one, so a
        // recipe that needs one is never begun and takes nothing.
        SmithingFixture quest(QuestMaterialRecipe());
        RequireSmithing(quest.smithing.Begin(quest.actor, quest.station, std::string(quest.recipe.id)).code == KCDC::Shared::RPC::SmithingCode::MissingMaterials, "a recipe that needs a quest material is refused");
        RequireSmithing(quest.inventory.TakeChanges().empty() && quest.EventNames().empty(), "the refused quest recipe takes nothing and raises nothing");
    }
} // namespace

void SmithingTests() {
    StationToolTests();
    EvaluatorTests();
    AdmissionTests();
    SuccessTests();
    FailureTests();
    ContextTests();
    AbandonTests();
    LifecycleTests();
    RefundTests();
}
