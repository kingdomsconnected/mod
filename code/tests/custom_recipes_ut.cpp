#include "features/crafting/alchemy_evaluator.h"
#include "features/crafting/smithing_evaluator.h"
#include "shared/game/combat_catalog.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/custom_recipe_registry.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/game/item_classes.h"
#include "shared/image/png_image.h"

#include <utils/hashing.h>
#include <utils/string_utils.h>

#include <algorithm>
#include <exception>
#include <function2/function2.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Recipes beyond the compiled catalog: a streamed mod's table patch rows and a script's
// definitions, compiled to the views the crafting rules read, and the registry those rules
// look recipes up in.

namespace {
    void Require(bool value, const std::string &message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    // Shipped classes: the Wine base, two herbs Hair o' the Dog takes, its first potion, a crude arrow.
    constexpr const char *kWine   = "d5efb270-948b-4a38-b391-38b2edd31c8d";
    constexpr const char *kHerb   = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    constexpr const char *kPotion = "7a1e8447-4449-473b-aac9-63f7a324fa0b";
    constexpr const char *kArrow  = "dfea5d01-b25c-414a-9ab4-6911a5f82118";

    constexpr const char *kAlchemyPatch = R"(<?xml version="1.0" encoding="us-ascii"?>
<database name="barbora">
	<AlchemyRecipes version="1">
		<AlchemyRecipe RecipeId="9001" BaseMaterial="Wine" UIUsage="mod_usage" UIName="mod_potion" UIIcon="potion_hair_o_dog" BaseQualityRewardXP="20">
			<Ingredients>
				<AlchemyIngredient IngredientItemId="DFEA5D01-B25C-414A-9AB4-6911A5F82118" Quantity="1" />
			</Ingredients>
			<Products>
				<AlchemyProduct Quality="1" ProductItemId="7a1e8447-4449-473b-aac9-63f7a324fa0b" MaxYield="2" />
			</Products>
			<Steps>
				<AlchemyRecipeStep StepId="1" StepType="Trivial" Condition="PotionBase() AND IngredientCount('dfea5d01-b25c-414a-9ab4-6911a5f82118')&gt;=1" />
				<AlchemyRecipeStep StepId="2" StepType="Hard" FeedbackPriority="3" UIText="mod_step" Condition="StrongBoilingTime('dfea5d01-b25c-414a-9ab4-6911a5f82118', 2.5) " />
			</Steps>
		</AlchemyRecipe>
		<AlchemyRecipe RecipeId="1" BaseMaterial="Water" UIName="mod_hair_o_dog" BaseQualityRewardXP="5">
			<Ingredients>
				<AlchemyIngredient IngredientItemId="b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4" Quantity="2" />
			</Ingredients>
			<Products>
				<AlchemyProduct Quality="1" ProductItemId="7a1e8447-4449-473b-aac9-63f7a324fa0b" MaxYield="1" />
			</Products>
			<Steps>
				<AlchemyRecipeStep StepId="1" StepType="Trivial" Condition="PotionBase() AND IngredientCount('b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4')==2" />
			</Steps>
		</AlchemyRecipe>
		<AlchemyRecipe RecipeId="9002" BaseMaterial="Water" UIName="broken" BaseQualityRewardXP="lots" />
	</AlchemyRecipes>
</database>)";

    constexpr const char *kSmithingPatch = R"(<database name="barbora">
	<BlacksmithRecipes version="1">
		<BlacksmithRecipe Id="mod_sword" UIIcon="shortswordCommon" Category="ShortSword" MinSkillLevel="3" WorkpieceId="sword">
			<UIName StringName="ui_nm_mod_sword" Text="Mod sword" />
			<Ingredients>
				<BlacksmithRecipeIngredient ItemClassId="dfea5d01-b25c-414a-9ab4-6911a5f82118" Amount="4" />
			</Ingredients>
			<Products>
				<BlacksmithRecipeProduct ItemClassId="7a1e8447-4449-473b-aac9-63f7a324fa0b" MinimalQuality="20" />
			</Products>
		</BlacksmithRecipe>
	</BlacksmithRecipes>
</database>)";

    nlohmann::json AlchemyDefinition() {
        return {
            {"kind", "alchemy"},
            {"id", 9100},
            {"name", "Arrow tea"},
            {"base", "Wine"},
            {"baseXP", 10},
            {"ingredients", {{{"item", kArrow}, {"amount", 1}}}},
            {"products", {{{"item", kPotion}, {"threshold", 1}, {"maxYield", 3}}}},
            {"steps", {{{"id", 1}, {"type", "Trivial"}, {"condition", std::string("PotionBase() AND IngredientCount('") + kArrow + "') >= 1"}}, {{"id", 2}, {"type", "Easy"}, {"condition", std::string("IsMilled('") + kArrow + "', 1)"}}}},
        };
    }

    std::shared_ptr<const KCDC::Shared::Game::CustomRecipe> Compiled(const nlohmann::json &value, std::string &error) {
        KCDC::Shared::Game::CustomRecipeDefinition definition;
        if (!KCDC::Shared::Game::CustomRecipeFromJson(value, definition, error)) {
            return nullptr;
        }
        return KCDC::Shared::Game::CustomRecipe::Compile(std::move(definition), error);
    }

    void ConditionTests() {
        std::deque<std::string> items;
        std::vector<KCDC::Shared::Game::AlchemyPredicate> predicates;
        std::string error;
        const std::string herb = std::string("'") + kHerb + "'";
        Require(KCDC::Shared::Game::ParseAlchemyCondition("PotionBase() AND IngredientCount(" + herb + ")>=1 AND IngredientCount( " + herb + " ) == 2 AND IsMilled(" + herb + ", 0) AND WeakBoilingTime(" + herb + ", 3) AND StrongBoilingTime(" + herb
                                                              + ",1.5) AND IsBaseDistilled(1) AND IsResultMilled(0) ",
                    items, predicates, error),
            "every shipped term parses: " + error);
        using Kind = KCDC::Shared::Game::AlchemyPredicateKind;
        Require(predicates.size() == 8 && predicates[0].kind == Kind::PotionBase && predicates[1].kind == Kind::CountAtLeast && predicates[1].value == 1.0F && predicates[2].kind == Kind::CountEqual && predicates[2].value == 2.0F, "counts read their operator");
        Require(predicates[3].kind == Kind::Milled && predicates[4].kind == Kind::WeakBoiling && predicates[4].value == 3.0F && predicates[5].kind == Kind::StrongBoiling && predicates[5].value == 1.5F, "timed terms read their value");
        Require(predicates[6].kind == Kind::BaseDistilled && predicates[6].value == 1.0F && predicates[7].kind == Kind::ResultMilled && predicates[7].value == 0.0F && predicates[1].item == kHerb, "flags and items read");

        for (const std::string &bad : std::vector<std::string> {"PotionBase( )", "IsMilled(" + herb + ", 2)", "IngredientCount(" + herb + ") > 1", "IngredientCount('B9DE1D84-A0C1-4B81-9F60-8D7FBB3CB9D4') >= 1", "PotionBase() OR IsResultMilled(1)",
                 "PotionBase()AND IsResultMilled(1)", "IsBaseDistilled(1) AND", "WeakBoilingTime(" + herb + ", -1)", "IngredientCount(" + herb + ") >= 1 trailing"}) {
            std::vector<KCDC::Shared::Game::AlchemyPredicate> refused;
            Require(!KCDC::Shared::Game::ParseAlchemyCondition(bad, items, refused, error) && refused.empty(), "refuses " + bad);
        }
    }

    void TableTests() {
        std::vector<KCDC::Shared::Game::CustomRecipeDefinition> rows;
        std::vector<std::string> problems;
        std::string error;
        Require(KCDC::Shared::Game::ParseRecipeTable(kAlchemyPatch, KCDC::Shared::Game::CraftKind::Alchemy, rows, problems, error), "an alchemy patch reads: " + error);
        Require(rows.size() == 2 && problems.size() == 1 && problems[0].find("9002") != std::string::npos, "a row it cannot read is named and left out");
        const auto &potion = rows[0];
        Require(potion.id == "9001" && potion.base == "Wine" && potion.usage == "mod_usage" && potion.baseXP == 20 && potion.icon == "potion_hair_o_dog", "the row's attributes");
        Require(potion.ingredients.size() == 1 && potion.ingredients[0].item == kArrow && potion.products[0].threshold == 1 && potion.products[0].maxYield == 2, "ingredient GUIDs are read in lower case");
        Require(potion.steps.size() == 2 && potion.steps[1].type == "Hard" && potion.steps[1].feedbackPriority == 3 && potion.steps[1].text == "mod_step", "the steps");

        rows.clear();
        Require(KCDC::Shared::Game::ParseRecipeTable(kSmithingPatch, KCDC::Shared::Game::CraftKind::Smithing, rows, problems, error) && rows.size() == 1, "a smithing patch reads");
        Require(rows[0].id == "mod_sword" && rows[0].name == "ui_nm_mod_sword" && rows[0].category == "ShortSword" && rows[0].workpiece == "sword" && rows[0].minSkill == 3 && rows[0].products[0].maxYield == 1, "the name is its key, the yield one");
        Require(!KCDC::Shared::Game::ParseRecipeTable(kSmithingPatch, KCDC::Shared::Game::CraftKind::Alchemy, rows, problems, error), "another table is refused whole");
    }

    /** A 1x1 PNG of `size` bytes whose chunks hold together, padded with a text chunk, as base64. */
    std::string PaddedPng(std::size_t size) {
        const auto chunk = [](const std::string &type, const std::string &data) {
            std::string out;
            for (const std::uint32_t value : {static_cast<std::uint32_t>(data.size())}) {
                out += {static_cast<char>(value >> 24), static_cast<char>(value >> 16), static_cast<char>(value >> 8), static_cast<char>(value)};
            }
            const std::string body  = type + data;
            const std::uint32_t crc = Framework::Utils::Hashing::CalculateCRC32(body.data(), body.size());
            return out + body + std::string {static_cast<char>(crc >> 24), static_cast<char>(crc >> 16), static_cast<char>(crc >> 8), static_cast<char>(crc)};
        };
        const std::string header = std::string("\x89PNG\r\n\x1a\n", 8) + chunk("IHDR", std::string("\0\0\0\1\0\0\0\1\x08\x06\0\0\0", 13));
        const std::string tail   = chunk("IDAT", std::string()) + chunk("IEND", std::string());
        const std::size_t text   = size > header.size() + tail.size() + 12 ? size - header.size() - tail.size() - 12 : 0;
        return Framework::Utils::StringUtils::Base64Encode(header + chunk("tEXt", std::string(text, 'k')) + tail);
    }

    void PictureTests() {
        constexpr const char *kTinyPng = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";
        std::string error;
        nlohmann::json pictured = AlchemyDefinition();
        pictured.erase("icon");
        pictured["iconImage"]    = kTinyPng;
        pictured["productImage"] = kTinyPng;
        pictured["recipeImage"]  = kTinyPng;
        KCDC::Shared::Game::CustomRecipeDefinition definition;
        Require(KCDC::Shared::Game::CustomRecipeFromJson(pictured, definition, error), "an alchemy recipe takes its three pictures: " + error);
        Require(definition.iconPng.size() == 70 && definition.productPng == definition.iconPng && definition.recipePng == definition.iconPng, "each picture's bytes are read");
        const std::string iconId = KCDC::Shared::Game::RecipeIconId(definition);
        Require(iconId.starts_with("kcdc_ri_") && iconId.size() == 24, "its icon is content-addressed");
        KCDC::Shared::Game::CustomRecipeDefinition again;
        Require(KCDC::Shared::Game::CustomRecipeFromJson(KCDC::Shared::Game::CustomRecipeToJson(definition), again, error) && again.recipeImage == kTinyPng && KCDC::Shared::Game::RecipeIconId(again) == iconId, "the pictures travel with the definition");
        Require(KCDC::Shared::Game::CustomRecipe::Compile(std::move(definition), error) != nullptr, "and it compiles: " + error);

        const auto refuses = [&](const char *what, const fu2::function<void(nlohmann::json &) const> &edit) {
            nlohmann::json value = pictured;
            edit(value);
            KCDC::Shared::Game::CustomRecipeDefinition refused;
            Require(!KCDC::Shared::Game::CustomRecipeFromJson(value, refused, error) && !error.empty(), std::string("refuses ") + what);
        };
        refuses("an icon name beside its own icon", [](nlohmann::json &value) {
            value["icon"] = "potion_hair_o_dog";
        });
        refuses("a picture that is not base64", [](nlohmann::json &value) {
            value["recipeImage"] = "not a png";
        });
        refuses("a picture that is not a PNG", [](nlohmann::json &value) {
            value["productImage"] = Framework::Utils::StringUtils::Base64Encode("GIF89a and the rest");
        });
        refuses("pictures past 256 KiB together", [&](nlohmann::json &value) {
            value["productImage"] = PaddedPng(130 * 1024);
            value["recipeImage"]  = PaddedPng(130 * 1024);
        });
        nlohmann::json one = pictured;
        one["recipeImage"] = PaddedPng(200 * 1024);
        one.erase("productImage");
        Require(KCDC::Shared::Game::CustomRecipeFromJson(one, definition, error), "one picture may take most of the allowance: " + error);

        nlohmann::json smithing = {{"kind", "smithing"}, {"id", "pictured_blade"}, {"name", "Blade"}, {"category", "ShortSword"}, {"workpiece", "sword"}, {"iconImage", kTinyPng}, {"ingredients", {{{"item", kArrow}, {"amount", 2}}}},
            {"products", {{{"item", kPotion}, {"threshold", 0}}}}};
        Require(KCDC::Shared::Game::CustomRecipeFromJson(smithing, definition, error), "a smithing recipe takes its own icon: " + error);
        smithing["recipeImage"] = kTinyPng;
        Require(!KCDC::Shared::Game::CustomRecipeFromJson(smithing, definition, error), "but no book pictures");

        nlohmann::json borrowing      = AlchemyDefinition();
        borrowing["id"]               = 9110;
        borrowing["bookPicturesFrom"] = 1;
        Require(KCDC::Shared::Game::CustomRecipeFromJson(borrowing, definition, error) && definition.bookPicturesFrom == "1", "an alchemy recipe borrows a game recipe's book pictures: " + error);
        Require(KCDC::Shared::Game::CustomRecipeToJson(definition).at("bookPicturesFrom") == 1, "and says so when it travels");
        borrowing["recipeImage"] = kTinyPng;
        Require(!KCDC::Shared::Game::CustomRecipeFromJson(borrowing, definition, error), "borrowing leaves its own book pictures out");
        borrowing.erase("recipeImage");
        borrowing["bookPicturesFrom"] = 0;
        Require(!KCDC::Shared::Game::CustomRecipeFromJson(borrowing, definition, error), "a borrowed id is a RecipeId");
        smithing.erase("recipeImage");
        smithing["bookPicturesFrom"] = 1;
        Require(!KCDC::Shared::Game::CustomRecipeFromJson(smithing, definition, error), "a smithing recipe has no book pictures to borrow");

        const KCDC::Shared::Image::ImageFit wide = KCDC::Shared::Image::FitImage(2048, 1024, KCDC::Shared::Game::kRecipeImageSide);
        Require(wide.width == 1024 && wide.height == 512 && wide.left == 0 && wide.top == 256, "a wide picture is fitted and centred on the book's square");
    }

    void DefinitionTests() {
        std::string error;
        const auto recipe = Compiled(AlchemyDefinition(), error);
        Require(recipe != nullptr, "a script's alchemy recipe compiles: " + error);
        const auto &view = recipe->Recipe();
        Require(view.kind == KCDC::Shared::Game::CraftKind::Alchemy && view.id == "9100" && view.base == "Wine" && view.ingredients.size() == 1 && view.ingredients[0].item == kArrow, "the recipe's views");
        const auto &program = recipe->Program();
        Require(program.recipe == "9100" && program.baseXP == 10 && program.steps.size() == 2 && program.steps[1].rewardXP == 12 && program.steps[1].predicates.size() == 1 && program.steps[1].predicates[0].item == kArrow, "the program's steps and XP");

        KCDC::Shared::Game::CustomRecipeDefinition again;
        Require(KCDC::Shared::Game::CustomRecipeFromJson(KCDC::Shared::Game::CustomRecipeToJson(recipe->Definition()), again, error) && KCDC::Shared::Game::CustomRecipeToJson(again) == KCDC::Shared::Game::CustomRecipeToJson(recipe->Definition()),
            "a definition reads back as written");

        const auto refuses = [&](const char *what, const fu2::function<void(nlohmann::json &) const> &edit) {
            nlohmann::json definition = AlchemyDefinition();
            edit(definition);
            Require(Compiled(definition, error) == nullptr && !error.empty(), std::string("refuses ") + what);
        };
        refuses("a string alchemy id", [](nlohmann::json &value) {
            value["id"] = "9100";
        });
        refuses("id zero", [](nlohmann::json &value) {
            value["id"] = 0;
        });
        refuses("an unknown base", [](nlohmann::json &value) {
            value["base"] = "Milk";
        });
        refuses("no steps", [](nlohmann::json &value) {
            value["steps"] = nlohmann::json::array();
        });
        refuses("a repeated step", [](nlohmann::json &value) {
            value["steps"][1]["id"] = 1;
        });
        refuses("step 29", [](nlohmann::json &value) {
            value["steps"][1]["id"] = 29;
        });
        refuses("an unknown step type", [](nlohmann::json &value) {
            value["steps"][1]["type"] = "Heroic";
        });
        refuses("an unsupported condition", [](nlohmann::json &value) {
            value["steps"][1]["condition"] = "IsHot()";
        });
        refuses("no ingredients", [](nlohmann::json &value) {
            value["ingredients"] = nlohmann::json::array();
        });
        refuses("an ingredient twice", [](nlohmann::json &value) {
            value["ingredients"].push_back(value["ingredients"][0]);
        });
        refuses("an amount of zero", [](nlohmann::json &value) {
            value["ingredients"][0]["amount"] = 0;
        });
        refuses("a malformed GUID", [](nlohmann::json &value) {
            value["ingredients"][0]["item"] = "arrow";
        });
        refuses("grade 5", [](nlohmann::json &value) {
            value["products"][0]["threshold"] = 5;
        });
        refuses("a yield of 65", [](nlohmann::json &value) {
            value["products"][0]["maxYield"] = 65;
        });
        refuses("a smithing field", [](nlohmann::json &value) {
            value["workpiece"] = "sword";
        });
        refuses("a skill an alchemy row does not read", [](nlohmann::json &value) {
            value["minSkill"] = 3;
        });
        refuses("an id the game's int cannot hold", [](nlohmann::json &value) {
            value["id"] = 2147483648U;
        });
        refuses("a condition past 1024 characters", [](nlohmann::json &value) {
            value["steps"][1]["condition"] = "PotionBase()" + std::string(1100, ' ');
        });

        nlohmann::json smithing = {{"kind", "smithing"}, {"id", "mod.blade-1"}, {"name", "Blade"}, {"category", "ShortSword"}, {"workpiece", "sword"}, {"ingredients", {{{"item", kArrow}, {"amount", 2}}}}, {"products", {{{"item", kPotion}, {"threshold", 0}}}}};
        Require(Compiled(smithing, error) != nullptr, "a script's smithing recipe compiles: " + error);
        smithing["workpiece"] = "spear";
        Require(Compiled(smithing, error) == nullptr, "refuses a workpiece the game has no blank for");
        smithing["workpiece"] = "sword";
        smithing["id"]        = "two words";
        Require(Compiled(smithing, error) == nullptr, "refuses a smithing id with a space");
    }

    /** A shipped melee weapon no shipped recipe makes, so its product class is derived. */
    std::string UnforgedWeapon() {
        for (const auto &weapon : KCDC::Shared::Game::g_CombatWeapons) {
            const auto text = KCDC::Shared::Game::CryGuidToText(weapon.itemClass.low, weapon.itemClass.high);
            const std::string guid(text.data(), text.size());
            const auto *item       = KCDC::Shared::Game::FindItemClass(guid);
            const auto *properties = KCDC::Shared::Game::FindInventoryPropertyClass(guid);
            if (item != nullptr && item->category == "MeleeWeapon" && properties != nullptr && !properties->questItem && KCDC::Shared::Game::FindSmithingProductClass(guid) == nullptr) {
                return guid;
            }
        }
        throw std::runtime_error("fixture: an unforged melee weapon");
    }

    void RegistryTests() {
        auto &registry = KCDC::Shared::Game::CustomRecipeRegistry::Get();
        registry.Reset();
        std::string error;

        // A mod's rows: a new recipe, and one replacing Hair o' the Dog in its place.
        std::vector<KCDC::Shared::Game::CustomRecipeDefinition> rows;
        std::vector<std::string> problems;
        Require(KCDC::Shared::Game::ParseRecipeTable(kAlchemyPatch, KCDC::Shared::Game::CraftKind::Alchemy, rows, problems, error), "fixture patch");
        std::vector<std::shared_ptr<const KCDC::Shared::Game::CustomRecipe>> recipes;
        for (auto &row : rows) {
            recipes.push_back(KCDC::Shared::Game::CustomRecipe::Compile(std::move(row), error));
            Require(recipes.back() != nullptr, "fixture row compiles: " + error);
        }
        Require(registry.ReplaceTables("mod", recipes).empty(), "the mod's rows are admitted");
        const auto *replaced = registry.Find(KCDC::Shared::Game::CraftKind::Alchemy, "1");
        Require(replaced != nullptr && replaced->name == "mod_hair_o_dog" && registry.Find(KCDC::Shared::Game::CraftKind::Alchemy, "9001") != nullptr, "a row with a shipped id replaces it");
        std::size_t listed = 0, position = 0, index = 0;
        registry.ForEach([&](const KCDC::Shared::Game::CraftRecipe &recipe, KCDC::Shared::Game::RecipeSource source) {
            if (recipe.kind == KCDC::Shared::Game::CraftKind::Alchemy && recipe.id == "1") {
                ++listed;
                Require(source == KCDC::Shared::Game::RecipeSource::Mod && &recipe == replaced, "listed as the mod's");
            }
        });
        registry.ForEachProgram([&](const KCDC::Shared::Game::AlchemyProgram &program, const KCDC::Shared::Game::CraftRecipe &) {
            if (program.recipe == "1") {
                position = index;
                Require(program.steps.size() == 1, "the program is the mod's");
            }
            ++index;
            return false;
        });
        Require(listed == 1 && position == 0 && index == KCDC::Shared::Game::g_AlchemyPrograms.size() + 1, "listed once, matched where the shipped one was, the new one after the game's");

        // The scripts'.
        const auto script = Compiled(AlchemyDefinition(), error);
        Require(registry.Register(script, error), "a script registers a recipe: " + error);
        Require(registry.Register(Compiled(AlchemyDefinition(), error), error), "the same definition again succeeds");
        nlohmann::json changed = AlchemyDefinition();
        changed["baseXP"]      = 11;
        Require(!registry.Register(Compiled(changed, error), error), "a different one under the id is refused");
        changed["id"] = 9001;
        Require(!registry.Register(Compiled(changed, error), error), "a mod's id is refused");
        changed["id"] = 1;
        Require(!registry.Register(Compiled(changed, error), error) && error.find("game") != std::string::npos, "a script cannot replace the game's recipe");
        changed["id"]                     = 9101;
        changed["ingredients"][0]["item"] = "00000000-0000-0000-0000-00000000c0de";
        Require(!registry.Register(Compiled(changed, error), error) && error.find("catalog") != std::string::npos, "an unknown item is refused");
        Require(registry.Registered().size() == 1 && registry.Registered()[0] == script, "Registered lists the script's alone");

        // The matcher finds the script's recipe for a brew of its own ingredients.
        std::vector<KCDC::Features::Crafting::AlchemyResource> brew = {{kWine, true}, {kArrow, false, true}};
        const auto match                                            = KCDC::Features::Crafting::AlchemyEvaluator::Match(brew, 0.2F, 10.0F, 0);
        Require(match.valid && match.recipe != nullptr && match.recipe->id == "9001", "the lower id is tried first");
        registry.RemoveTables("mod");
        const auto alone = KCDC::Features::Crafting::AlchemyEvaluator::Match(brew, 0.2F, 10.0F, 0);
        Require(alone.recipe != nullptr && alone.recipe->id == "9100" && alone.successfulSteps == std::vector<std::uint32_t> {1, 2} && alone.stepXP == 12 && alone.baseXP == 10, "the script's recipe, both steps passed");
        // The game's matcher walks its rows by RecipeId: a recipe registered later with a lower id comes first.
        nlohmann::json earlier = AlchemyDefinition();
        earlier["id"]          = 9050;
        earlier["name"]        = "Arrow tea, earlier";
        Require(registry.Register(Compiled(earlier, error), error), "a second script recipe for the same brew: " + error);
        const auto ordered = KCDC::Features::Crafting::AlchemyEvaluator::Match(brew, 0.2F, 10.0F, 0);
        Require(ordered.recipe != nullptr && ordered.recipe->id == "9050", "matched by id, not by registration");
        std::size_t previous = 0;
        bool ascending       = true;
        registry.ForEachProgram([&](const KCDC::Shared::Game::AlchemyProgram &program, const KCDC::Shared::Game::CraftRecipe &) {
            const std::size_t id = std::stoul(std::string(program.recipe));
            ascending            = ascending && id > previous;
            previous             = id;
            return false;
        });
        Require(ascending, "every program in ascending id order");

        nlohmann::json borrower      = AlchemyDefinition();
        borrower["id"]               = 9070;
        borrower["bookPicturesFrom"] = 9100;
        Require(!registry.Register(Compiled(borrower, error), error) && error.find("game") != std::string::npos, "pictures are borrowed from the game's recipes alone");
        borrower["bookPicturesFrom"] = 1;
        Require(registry.Register(Compiled(borrower, error), error), "a game recipe's pictures are borrowed: " + error);

        nlohmann::json test = AlchemyDefinition();
        test["id"]          = 1001;
        Require(!registry.Register(Compiled(test, error), error) && error.find("game") != std::string::npos, "the game's own test rows keep their ids");
        const auto quest = std::find_if(KCDC::Shared::Game::g_InventoryPropertyClasses.begin(), KCDC::Shared::Game::g_InventoryPropertyClasses.end(), [](const KCDC::Shared::Game::InventoryPropertyClass &entry) {
            return entry.questItem && KCDC::Shared::Game::FindItemClass(entry.guid) != nullptr;
        });
        Require(quest != KCDC::Shared::Game::g_InventoryPropertyClasses.end(), "fixture: a quest item");
        test["id"]                  = 9060;
        test["products"][0]["item"] = std::string(quest->guid);
        Require(!registry.Register(Compiled(test, error), error) && error.find("inventory") != std::string::npos, "a potion the inventory cannot hold is refused");
        Require(registry.Find(KCDC::Shared::Game::CraftKind::Alchemy, "1") == KCDC::Shared::Game::FindCraftRecipe(KCDC::Shared::Game::CraftKind::Alchemy, "1") && replaced->name == "mod_hair_o_dog", "the game's recipe is back, and a pointer taken earlier still reads");

        // Two mods with one id: the resource whose name sorts first keeps it, whichever started first.
        const auto named = [&](const char *name) {
            nlohmann::json definition = AlchemyDefinition();
            definition["id"]          = 9300;
            definition["name"]        = name;
            KCDC::Shared::Game::CustomRecipeDefinition parsed;
            Require(KCDC::Shared::Game::CustomRecipeFromJson(definition, parsed, error), "fixture definition");
            return std::vector<std::shared_ptr<const KCDC::Shared::Game::CustomRecipe>> {KCDC::Shared::Game::CustomRecipe::Compile(std::move(parsed), error)};
        };
        Require(registry.ReplaceTables("zeta", named("zeta's")).empty() && registry.Find(KCDC::Shared::Game::CraftKind::Alchemy, "9300")->name == "zeta's", "the only mod has it");
        Require(registry.ReplaceTables("alpha", named("alpha's")).empty() && registry.Find(KCDC::Shared::Game::CraftKind::Alchemy, "9300")->name == "alpha's", "a mod whose name sorts first takes it");
        Require(registry.ReplaceTables("zeta", named("zeta's")).size() == 1, "the other mod's row is refused");
        registry.RemoveTables("alpha");
        Require(registry.Find(KCDC::Shared::Game::CraftKind::Alchemy, "9300")->name == "zeta's", "and comes back when the first leaves");
        registry.RemoveTables("zeta");

        // Smithing: rules and product classes beyond the compiled tables.
        const std::string weapon = UnforgedWeapon();
        const auto product       = registry.ProductClass(weapon);
        Require(product && product->kind == KCDC::Shared::Game::SmithingProductKind::MeleeWeapon && product->weaponClass == KCDC::Shared::Game::FindCombatWeapon(KCDC::Shared::Game::ItemClassIdFromGuid(weapon))->weaponClass, "a weapon's class comes from the combat catalog");
        const auto rule = registry.MaterialRule(kArrow);
        Require(rule && rule->divisible == KCDC::Shared::Game::FindInventoryPropertyClass(kArrow)->nativeStackable && !rule->questItem, "a material's rule comes from the class flags");
        nlohmann::json sword = {{"kind", "smithing"}, {"id", "custom_sword"}, {"name", "Custom sword"}, {"category", "ShortSword"}, {"workpiece", "sword"}, {"minSkill", 2}, {"ingredients", {{{"item", kArrow}, {"amount", 2}}}},
            {"products", {{{"item", weapon}, {"threshold", 10}}}}};
        Require(registry.Register(Compiled(sword, error), error), "a smithing recipe making any weapon registers: " + error);
        const auto *forged = registry.Find(KCDC::Shared::Game::CraftKind::Smithing, "custom_sword");
        Require(forged != nullptr && KCDC::Features::Crafting::SmithingEvaluator::Admits(*forged, {}) == std::optional<std::string>("skillTooLow"), "the smithy's rules read it");
        KCDC::Features::Crafting::SmithingCharacterContext smith;
        smith.skill = 5;
        Require(!KCDC::Features::Crafting::SmithingEvaluator::Admits(*forged, smith) && KCDC::Features::Crafting::SmithingEvaluator::Product(*forged, 0.5F, smith).has_value(), "and make its product");
        smith.hardenedSteel = true;
        const auto hardened = KCDC::Features::Crafting::SmithingEvaluator::Product(*forged, 0.5F, smith);
        Require(hardened.has_value() && hardened->metadata.contains("onEquipBuffs"), "a Hardened Steel smith's weapon keeps the buff, whichever recipe forged it");
        sword["id"]                  = "potion_forge";
        sword["products"][0]["item"] = "00000000-0000-0000-0000-00000000c0de";
        Require(!registry.Register(Compiled(sword, error), error), "an unknown product is refused");

        registry.Reset();
        Require(registry.Registered().empty() && registry.Find(KCDC::Shared::Game::CraftKind::Smithing, "custom_sword") == nullptr, "reset empties it");
    }
} // namespace

int RunCustomRecipeSuite() {
    try {
        ConditionTests();
        TableTests();
        DefinitionTests();
        PictureTests();
        RegistryTests();
    }
    catch (const std::exception &failure) {
        KCDC::Shared::Game::CustomRecipeRegistry::Get().Reset();
        std::cerr << "FAILED: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "custom recipe tests passed\n";
    return 0;
}
