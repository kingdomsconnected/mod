#include "features/crafting/alchemy_evaluator.h"
#include "shared/game/crafting_catalog.h"

#include <limits>
#include <stdexcept>
#include <vector>

namespace {
    void Require(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void AlchemyPredicates() {
        Require(KCDC::Shared::Game::g_AlchemyPrograms.size() == 31, "all production alchemy programs compiled");
        std::size_t stepCount = 0;
        for (const auto &program : KCDC::Shared::Game::g_AlchemyPrograms) {
            stepCount += program.steps.size();
        }
        Require(stepCount == 270, "all production conditions compiled");
        std::vector<KCDC::Features::Crafting::AlchemyResource> brew {{"e96d768e-04e5-4480-9197-1c256d642ddc", true, false, false, 0, 0}, {"b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4", false, false, false, 30, 0}, {"9b771c16-c0b1-438c-aeda-8c4d3ce28465", false, false, false, 30, 0},
            {"7da04bba-0564-42da-bcf1-9a2fc5faf025", false, true, false, 0, 0}};
        auto result = KCDC::Features::Crafting::AlchemyEvaluator::Match(brew, 0.25F, 10.0F, 0);
        Require(result.valid && result.contentAllowed && result.recipe != nullptr && result.recipe->id == "1", "hair of the dog identified by actual resources");
        Require(result.successfulSteps.size() == 9 && result.totalSteps == 9 && result.stepXP == 120 && result.baseXP == 18, "manual recipe fixture meets nine conditions and table XP");
        brew[1].item = "2bb1c148-8ee1-42bc-8a93-8c456a57eba5";
        result       = KCDC::Features::Crafting::AlchemyEvaluator::Match(brew, 0.25F, 10.0F, 0);
        Require(result.recipe != nullptr && result.recipe->id == "1" && result.successfulSteps.size() == 9, "dried sage satisfies the fresh sage predicates");
        Require(!KCDC::Features::Crafting::AlchemyEvaluator::MatchesIngredient("b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4", "2bb1c148-8ee1-42bc-8a93-8c456a57eba5"), "herb equivalence is directional");
        brew.push_back(brew[1]);
        result = KCDC::Features::Crafting::AlchemyEvaluator::Match(brew, 0.25F, 10.0F, 0);
        Require(result.recipe != nullptr && result.recipe->id == "1" && result.successfulSteps.size() == 8 && result.stepXP == 108, "excess ingredient fails quantity step but preserves identification");

        const auto *recipe = KCDC::Shared::Game::FindCraftRecipe(KCDC::Shared::Game::CraftKind::Alchemy, "1");
        const KCDC::Shared::Game::AlchemyPredicate weak {KCDC::Shared::Game::AlchemyPredicateKind::WeakBoiling, "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4", 3};
        brew.back().weakSeconds = 10;
        Require(!KCDC::Features::Crafting::AlchemyEvaluator::Predicate(weak, *recipe, brew, 0.25F, 10.0F), "late herb addition changes mean exposure");
        brew.back().weakSeconds = 15;
        Require(KCDC::Features::Crafting::AlchemyEvaluator::Predicate(weak, *recipe, brew, 0.25F, 10.0F), "boiling tolerance includes its exact boundary");
        brew.back().weakSeconds = 14.99F;
        Require(!KCDC::Features::Crafting::AlchemyEvaluator::Predicate(weak, *recipe, brew, 0.25F, 10.0F), "just below the exposure boundary fails");
        const KCDC::Shared::Game::AlchemyPredicate milled {KCDC::Shared::Game::AlchemyPredicateKind::Milled, "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4", 1};
        brew[1].milled = true;
        Require(!KCDC::Features::Crafting::AlchemyEvaluator::Predicate(milled, *recipe, brew, 0.25F, 10.0F), "mixed milling does not count as all milled");
        brew.back().milled = true;
        Require(KCDC::Features::Crafting::AlchemyEvaluator::Predicate(milled, *recipe, brew, 0.25F, 10.0F), "every matching dried unit participates in milling");
        brew[0].item = "34bd1d0b-1203-42a0-b9b4-d3585a4d9b48";
        const KCDC::Shared::Game::AlchemyPredicate base {KCDC::Shared::Game::AlchemyPredicateKind::PotionBase, "", 0};
        Require(!KCDC::Features::Crafting::AlchemyEvaluator::Predicate(base, *recipe, brew, 0.25F, 10.0F), "oil cannot replace required water");
        brew[1].weakSeconds = std::numeric_limits<float>::quiet_NaN();
        Require(!KCDC::Features::Crafting::AlchemyEvaluator::Match(brew, 0.25F, 10.0F, 0).valid, "nonfinite mechanics state refused");
        result = KCDC::Features::Crafting::AlchemyEvaluator::Match({}, 0.25F, 10.0F, 0);
        Require(result.valid && result.recipe == nullptr, "unknown brew is distinct from malformed state");
    }

    void Catalog() {
        Require(KCDC::Shared::Game::g_CraftRecipes.size() == 92, "production recipe row count");
        Require(KCDC::Shared::Game::g_CraftStations.size() == 48, "placed station count");
        Require(KCDC::Shared::Game::g_CraftStationBooks.size() == 20, "linked alchemy recipe books compiled");
        bool defaultBook = false;
        for (const auto &book : KCDC::Shared::Game::g_CraftStationBooks) {
            defaultBook |= book.station == "kutnohorsko:0148ef920dcb72aa" && book.slotGuid == 0x0227b618aa5dc205ULL;
        }
        Require(defaultBook, "default test station retains its authored book slot");
        std::size_t baseAlchemy = 0, baseSmithing = 0;
        for (const auto &recipe : KCDC::Shared::Game::g_CraftRecipes) {
            if (KCDC::Shared::Game::CraftDLCEnabled(recipe.dlc, 0)) {
                if (recipe.kind == KCDC::Shared::Game::CraftKind::Alchemy) {
                    ++baseAlchemy;
                }
                else {
                    ++baseSmithing;
                }
            }
        }
        Require(baseAlchemy == 27 && baseSmithing == 47, "no paid DLC recipe policy includes free racing content");
        Require(!KCDC::Shared::Game::CraftDLCEnabled(8, 0xffff), "unpublished content never enabled");
        Require(!KCDC::Shared::Game::CraftDLCEnabled(5, 0) && KCDC::Shared::Game::CraftDLCEnabled(5, 1 << 5), "configured Forge DLC policy");
        Require(KCDC::Shared::Game::FindCraftRecipe(KCDC::Shared::Game::CraftKind::Alchemy, "57")->dlc == 9, "painter recipe belongs to Brushes with Death");
    }
} // namespace

void AlchemyCatalogTests() {
    AlchemyPredicates();
    Catalog();
}
