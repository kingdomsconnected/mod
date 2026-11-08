#include "custom_recipe_registry.h"

#include "combat_catalog.h"
#include "custom_item_registry.h"
#include "inventory_property_classes.h"
#include "item_classes.h"
#include "progression_catalog.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>

namespace KCDC::Shared::Game {
    namespace {
        // Rows the game loads from AlchemyRecipe__autotests.xml and BlacksmithRecipes__autotests.xml,
        // which the compiled catalog leaves out: a script's recipe under one of these ids would never
        // reach a client's tables.
        constexpr std::array<std::string_view, 2> kShippedTestAlchemy  = {"1001", "1002"};
        constexpr std::array<std::string_view, 3> kShippedTestSmithing = {"basic_axe", "basic_horseshoe", "basic_longsword"};

        bool ShippedTestRecipe(CraftKind kind, std::string_view id) {
            return kind == CraftKind::Alchemy ? std::find(kShippedTestAlchemy.begin(), kShippedTestAlchemy.end(), id) != kShippedTestAlchemy.end() : std::find(kShippedTestSmithing.begin(), kShippedTestSmithing.end(), id) != kShippedTestSmithing.end();
        }

        std::int64_t RecipeNumber(std::string_view id) {
            std::int64_t value = 0;
            std::from_chars(id.data(), id.data() + id.size(), value);
            return value;
        }
    } // namespace

    CustomRecipeRegistry &CustomRecipeRegistry::Get() {
        static CustomRecipeRegistry registry;
        return registry;
    }

    std::vector<std::string> CustomRecipeRegistry::ReplaceTables(const std::string &source, std::vector<std::shared_ptr<const CustomRecipe>> recipes) {
        if (source.empty()) {
            return {};
        }
        if (const auto previous = _tables.find(source); previous != _tables.end()) {
            Retire(previous->second);
            _tables.erase(previous);
        }
        if (!recipes.empty()) {
            _tables.emplace(source, std::move(recipes));
        }
        return Rebuild(source);
    }

    void CustomRecipeRegistry::RemoveTables(const std::string &source) {
        const auto previous = _tables.find(source);
        if (previous == _tables.end()) {
            return;
        }
        Retire(previous->second);
        _tables.erase(previous);
        Rebuild({});
    }

    void CustomRecipeRegistry::Retire(std::vector<std::shared_ptr<const CustomRecipe>> &recipes) {
        _retired.insert(_retired.end(), std::make_move_iterator(recipes.begin()), std::make_move_iterator(recipes.end()));
        recipes.clear();
    }

    std::vector<std::string> CustomRecipeRegistry::Rebuild(std::string_view source) {
        std::vector<std::string> refused;
        _entries.clear();
        for (const auto &[name, recipes] : _tables) {
            for (const auto &recipe : recipes) {
                std::string error;
                if (!Admit(*recipe, false, error)) {
                    if (name == source) {
                        refused.push_back("recipe '" + recipe->Definition().id + "': " + error);
                    }
                    continue;
                }
                _entries.push_back({recipe, name, FindCraftRecipe(recipe->Recipe().kind, recipe->Recipe().id) != nullptr});
            }
        }
        for (const auto &recipe : _scripts) {
            _entries.push_back({recipe, {}, false});
        }
        ++_revision;
        return refused;
    }

    bool CustomRecipeRegistry::Register(std::shared_ptr<const CustomRecipe> recipe, std::string &error) {
        if (recipe == nullptr) {
            error = "no recipe";
            return false;
        }
        const CraftRecipe &view = recipe->Recipe();
        if (const Entry *existing = FindEntry(view.kind, view.id); existing != nullptr && existing->source.empty()) {
            if (CustomRecipeToJson(existing->recipe->Definition()) == CustomRecipeToJson(recipe->Definition())) {
                return true;
            }
            error = "a different recipe " + std::string(view.id) + " is already registered";
            return false;
        }
        if (!Admit(*recipe, true, error)) {
            return false;
        }
        _scripts.push_back(recipe);
        _entries.push_back({std::move(recipe), {}, false});
        ++_revision;
        return true;
    }

    void CustomRecipeRegistry::Reset() {
        for (auto &[name, recipes] : _tables) {
            Retire(recipes);
        }
        _tables.clear();
        Retire(_scripts);
        _entries.clear();
        ++_revision;
    }

    const CustomRecipeRegistry::Entry *CustomRecipeRegistry::FindEntry(CraftKind kind, std::string_view id) const {
        const auto found = std::find_if(_entries.begin(), _entries.end(), [&](const Entry &entry) {
            return entry.recipe->Recipe().kind == kind && entry.recipe->Recipe().id == id;
        });
        return found != _entries.end() ? &*found : nullptr;
    }

    bool CustomRecipeRegistry::Admit(const CustomRecipe &recipe, bool script, std::string &error) const {
        const CraftRecipe &view = recipe.Recipe();
        // A script adds recipes; replacing the game's is a table patch's business, as the game's loader has it.
        if (script && (FindCraftRecipe(view.kind, view.id) != nullptr || ShippedTestRecipe(view.kind, view.id))) {
            error = "recipe " + std::string(view.id) + " is one of the game's";
            return false;
        }
        const auto scripted = std::any_of(_scripts.begin(), _scripts.end(), [&view](const std::shared_ptr<const CustomRecipe> &other) {
            return other->Recipe().kind == view.kind && other->Recipe().id == view.id;
        });
        if (FindEntry(view.kind, view.id) != nullptr || (!script && scripted)) {
            error = "another resource already adds recipe " + std::string(view.id);
            return false;
        }
        // The game ships both pictures for each of its own recipes, and only for those.
        if (const std::string &borrowed = recipe.Definition().bookPicturesFrom; !borrowed.empty() && FindCraftRecipe(CraftKind::Alchemy, borrowed) == nullptr) {
            error = "bookPicturesFrom " + borrowed + " is not one of the game's alchemy recipes";
            return false;
        }
        if (!view.perk.empty() && FindPerkIndex(PerkGuidFromText(view.perk)) == RPC::kNoPerk) {
            error = "perk " + std::string(view.perk) + " is not one of the game's perks";
            return false;
        }
        const auto known = [&error](std::string_view item) {
            // A custom item's class differs on the client, where the game's own tables cannot name it.
            if (CustomItemRegistry::Get().Find(item) != nullptr) {
                error = "item " + std::string(item) + " is a custom item, which recipes cannot use";
                return false;
            }
            if (FindItemClass(item) == nullptr) {
                error = "item " + std::string(item) + " is not in the item catalog";
                return false;
            }
            return true;
        };
        for (const auto &ingredient : view.ingredients) {
            if (!known(ingredient.item)) {
                return false;
            }
        }
        for (const auto &product : view.products) {
            if (!known(product.item)) {
                return false;
            }
            if (view.kind != CraftKind::Smithing) {
                // The finished batch goes into the inventory, which holds no quest item.
                const InventoryPropertyClass *properties = FindInventoryPropertyClass(product.item);
                if (properties == nullptr || properties->questItem) {
                    error = "product " + std::string(product.item) + " is not an item the inventory can hold";
                    return false;
                }
                continue;
            }
            // What SmithingEvaluator::Product can settle: a class of a kind the native outcome makes,
            // whose condition only a melee weapon's remap may change, and one the inventory will hold.
            const auto made                          = ProductClass(product.item);
            const InventoryPropertyClass *properties = FindInventoryPropertyClass(product.item);
            if (!made || properties == nullptr || made->questItem || (properties->canHealthChange && made->kind != SmithingProductKind::MeleeWeapon)) {
                error = "product " + std::string(product.item) + " is not a melee weapon, armor or misc item a smithy can make";
                return false;
            }
        }
        return true;
    }

    const CraftRecipe *CustomRecipeRegistry::Find(CraftKind kind, std::string_view id) const {
        if (const Entry *entry = FindEntry(kind, id)) {
            return &entry->recipe->Recipe();
        }
        return FindCraftRecipe(kind, id);
    }

    void CustomRecipeRegistry::ForEach(const fu2::function<void(const CraftRecipe &, RecipeSource) const> &visit) const {
        for (const CraftRecipe &recipe : g_CraftRecipes) {
            const Entry *entry = FindEntry(recipe.kind, recipe.id);
            visit(entry != nullptr ? entry->recipe->Recipe() : recipe, entry != nullptr ? RecipeSource::Mod : RecipeSource::Game);
        }
        for (const Entry &entry : _entries) {
            if (!entry.replaces) {
                visit(entry.recipe->Recipe(), entry.source.empty() ? RecipeSource::Script : RecipeSource::Mod);
            }
        }
    }

    void CustomRecipeRegistry::ForEachProgram(const fu2::function<bool(const AlchemyProgram &, const CraftRecipe &) const> &visit) const {
        // The game's matcher (0x182E2BD10) walks its table's rows, which RebuildIndex keeps sorted by
        // RecipeId, and stops at the first that identifies the brew. The shipped table is already in
        // that order; a custom recipe goes where its id puts it.
        struct Candidate {
            std::int64_t id;
            const AlchemyProgram *program;
            const CraftRecipe *recipe;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(g_AlchemyPrograms.size() + _entries.size());
        for (const AlchemyProgram &program : g_AlchemyPrograms) {
            const Entry *entry = FindEntry(CraftKind::Alchemy, program.recipe);
            if (entry != nullptr) {
                candidates.push_back({RecipeNumber(program.recipe), &entry->recipe->Program(), &entry->recipe->Recipe()});
            }
            else if (const CraftRecipe *recipe = FindCraftRecipe(CraftKind::Alchemy, program.recipe)) {
                candidates.push_back({RecipeNumber(program.recipe), &program, recipe});
            }
        }
        for (const Entry &entry : _entries) {
            if (!entry.replaces && entry.recipe->Recipe().kind == CraftKind::Alchemy) {
                candidates.push_back({RecipeNumber(entry.recipe->Recipe().id), &entry.recipe->Program(), &entry.recipe->Recipe()});
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &left, const Candidate &right) {
            return left.id < right.id;
        });
        for (const Candidate &candidate : candidates) {
            if (visit(*candidate.program, *candidate.recipe)) {
                return;
            }
        }
    }

    const std::vector<std::shared_ptr<const CustomRecipe>> &CustomRecipeRegistry::Registered() const {
        return _scripts;
    }

    std::optional<SmithingMaterialRule> CustomRecipeRegistry::MaterialRule(std::string_view item) const {
        if (const SmithingMaterialRule *rule = FindSmithingMaterialRule(item)) {
            return *rule;
        }
        // The generator's rule, from the catalog's flags: ConsumeRecipeMaterials 0x1812C1950 skips
        // IsQuestItem and on a failure debits only a class whose slot 7 says divisible.
        const InventoryPropertyClass *properties = FindInventoryPropertyClass(item);
        if (FindItemClass(item) == nullptr || properties == nullptr) {
            return std::nullopt;
        }
        return SmithingMaterialRule {item, properties->questItem, properties->nativeStackable};
    }

    std::optional<SmithingProductClass> CustomRecipeRegistry::ProductClass(std::string_view item) const {
        if (const SmithingProductClass *product = FindSmithingProductClass(item)) {
            return *product;
        }
        const ItemClassEntry *entry              = FindItemClass(item);
        const InventoryPropertyClass *properties = FindInventoryPropertyClass(item);
        if (entry == nullptr || properties == nullptr) {
            return std::nullopt;
        }
        if (entry->category == "MeleeWeapon") {
            // RPGMinigames slot 76 reads the authored Class; a mod's weapon has none the server knows.
            const CombatWeaponEntry *weapon = FindCombatWeapon(ItemClassIdFromGuid(item));
            return SmithingProductClass {item, SmithingProductKind::MeleeWeapon, properties->questItem, weapon != nullptr ? weapon->weaponClass : -1};
        }
        if (entry->category == "Armor") {
            return SmithingProductClass {item, SmithingProductKind::Armor, properties->questItem, -1};
        }
        if (entry->category == "MiscItem") {
            return SmithingProductClass {item, SmithingProductKind::MiscItem, properties->questItem, -1};
        }
        return std::nullopt;
    }
} // namespace KCDC::Shared::Game
