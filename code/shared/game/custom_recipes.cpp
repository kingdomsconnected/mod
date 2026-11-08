#include "custom_recipes.h"

#include "dlc_catalog.h"
#include "shared/features/crafting/smithing_rpc.h"
#include "shared/image/png_image.h"
#include "shared/item_stack.h"
#include "shared/xml/mini_xml.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <function2/function2.hpp>
#include <limits>
#include <set>
#include <utility>
#include <utils/crypto.h>

namespace KCDC::Shared::Game {
    namespace {
        // A step's id is its bit in the knowledge mask, and the recipe book's query
        // (AlchemyCharacterView::KnowledgeRange) answers bits 0 to 28. The shipped table stops at 11.
        constexpr std::uint32_t kMaximumAlchemyStep = 28;
        // AlchemyModule::kMaximumProductYield, which the server lives with and this file cannot see.
        constexpr std::uint32_t kMaximumAlchemyYield = 64;
        constexpr std::size_t kMaximumText           = 256;
        // Four times the shipped table's longest (268), and small enough that a whole recipe stays a
        // few tens of kilobytes on the wire.
        constexpr std::size_t kMaximumCondition = 1024;
        constexpr std::size_t kMaximumRows      = 64;

        // AlchemyPotionBase.xml's BaseType values.
        constexpr std::array<std::string_view, 4> kPotionBases = {"Oil", "Spiritus", "Water", "Wine"};
        // BlacksmithRecipes.xml's Category values and BlacksmithWorkpieces.xml's ids.
        constexpr std::array<std::string_view, 4> kSmithingCategories = {"Axe", "LongSword", "Other", "ShortSword"};
        constexpr std::array<std::string_view, 4> kWorkpieces         = {"axe", "horseshoe", "longsword", "sword"};

        template <std::size_t N>
        bool In(const std::array<std::string_view, N> &set, std::string_view value) {
            return std::find(set.begin(), set.end(), value) != set.end();
        }

        std::string Lower(std::string_view text) {
            std::string result(text);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return result;
        }

        bool ReadUnsigned(std::string_view text, std::uint32_t &out) {
            std::uint32_t value = 0;
            const auto result   = std::from_chars(text.data(), text.data() + text.size(), value);
            if (text.empty() || result.ec != std::errc() || result.ptr != text.data() + text.size()) {
                return false;
            }
            out = value;
            return true;
        }

        bool ReadOptionalUnsigned(std::string_view text, std::uint32_t fallback, std::uint32_t &out) {
            if (text.empty()) {
                out = fallback;
                return true;
            }
            return ReadUnsigned(text, out);
        }

        /** Walks one condition term the way the generator's regular expressions do. */
        class Cursor final {
          public:
            explicit Cursor(std::string_view text): _text(text) {}

            void Space() {
                while (_at < _text.size() && std::isspace(static_cast<unsigned char>(_text[_at])) != 0) {
                    ++_at;
                }
            }

            bool Literal(std::string_view literal) {
                Space();
                if (_text.substr(_at, literal.size()) != literal) {
                    return false;
                }
                _at += literal.size();
                return true;
            }

            bool Guid(std::string &out) {
                Space();
                if (_text.substr(_at, 1) != "'" || _at + 1 + RPC::kItemClassGuidLength >= _text.size() || _text[_at + 1 + RPC::kItemClassGuidLength] != '\'') {
                    return false;
                }
                const std::string_view guid = _text.substr(_at + 1, RPC::kItemClassGuidLength);
                // The shipped conditions spell every GUID in lower case.
                if (!RPC::IsItemClassGuidText(guid) || Lower(guid) != guid) {
                    return false;
                }
                out = std::string(guid);
                _at += RPC::kItemClassGuidLength + 2;
                return true;
            }

            bool Number(bool fraction, float &out) {
                Space();
                const std::size_t start = _at;
                while (_at < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_at])) != 0) {
                    ++_at;
                }
                if (_at == start) {
                    return false;
                }
                if (fraction && _at < _text.size() && _text[_at] == '.') {
                    const std::size_t point = ++_at;
                    while (_at < _text.size() && std::isdigit(static_cast<unsigned char>(_text[_at])) != 0) {
                        ++_at;
                    }
                    if (_at == point) {
                        return false;
                    }
                }
                const auto result = std::from_chars(_text.data() + start, _text.data() + _at, out);
                return result.ec == std::errc() && std::isfinite(out);
            }

            bool End() {
                Space();
                return _at == _text.size();
            }

          private:
            std::string_view _text;
            std::size_t _at = 0;
        };

        std::string_view Trim(std::string_view text) {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
                text.remove_prefix(1);
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
                text.remove_suffix(1);
            }
            return text;
        }

        /** The terms of `\s+AND\s+`; no term's own text can hold the keyword between spaces. */
        std::vector<std::string_view> Terms(std::string_view condition) {
            std::vector<std::string_view> terms;
            std::size_t start = 0;
            for (std::size_t at = condition.find("AND"); at != std::string_view::npos; at = condition.find("AND", at + 3)) {
                const bool before = at > 0 && std::isspace(static_cast<unsigned char>(condition[at - 1])) != 0;
                const bool after  = at + 3 < condition.size() && std::isspace(static_cast<unsigned char>(condition[at + 3])) != 0;
                if (before && after) {
                    terms.push_back(condition.substr(start, at - start));
                    start = at + 3;
                }
            }
            terms.push_back(condition.substr(start));
            return terms;
        }

        bool ParseTerm(std::string_view term, std::deque<std::string> &items, AlchemyPredicate &out) {
            if (term == "PotionBase()") {
                out = {AlchemyPredicateKind::PotionBase, {}, 0.0F};
                return true;
            }
            std::string guid;
            float value = 0.0F;
            {
                Cursor cursor(term);
                if (cursor.Literal("IngredientCount") && cursor.Literal("(") && cursor.Guid(guid) && cursor.Literal(")")) {
                    const bool equal = cursor.Literal("==");
                    if ((equal || cursor.Literal(">=")) && cursor.Number(false, value) && cursor.End()) {
                        out = {equal ? AlchemyPredicateKind::CountEqual : AlchemyPredicateKind::CountAtLeast, items.emplace_back(std::move(guid)), value};
                        return true;
                    }
                    return false;
                }
            }
            constexpr std::array<std::pair<std::string_view, AlchemyPredicateKind>, 3> kTimed = {{{"IsMilled", AlchemyPredicateKind::Milled}, {"WeakBoilingTime", AlchemyPredicateKind::WeakBoiling}, {"StrongBoilingTime", AlchemyPredicateKind::StrongBoiling}}};
            for (const auto &[name, kind] : kTimed) {
                Cursor cursor(term);
                if (!cursor.Literal(name) || !cursor.Literal("(")) {
                    continue;
                }
                if (!cursor.Guid(guid) || !cursor.Literal(",") || !cursor.Number(true, value) || !cursor.Literal(")") || !cursor.End()) {
                    return false;
                }
                if (kind == AlchemyPredicateKind::Milled && value != 0.0F && value != 1.0F) {
                    return false;
                }
                out = {kind, items.emplace_back(std::move(guid)), value};
                return true;
            }
            constexpr std::array<std::pair<std::string_view, AlchemyPredicateKind>, 2> kFlags = {{{"IsBaseDistilled", AlchemyPredicateKind::BaseDistilled}, {"IsResultMilled", AlchemyPredicateKind::ResultMilled}}};
            for (const auto &[name, kind] : kFlags) {
                Cursor cursor(term);
                if (!cursor.Literal(name) || !cursor.Literal("(")) {
                    continue;
                }
                if (!cursor.Number(false, value) || (value != 0.0F && value != 1.0F) || !cursor.Literal(")") || !cursor.End()) {
                    return false;
                }
                out = {kind, {}, value};
                return true;
            }
            return false;
        }

        bool ReadAlchemyRow(const Xml::Element &row, CustomRecipeDefinition &out, std::string &error) {
            out.kind          = CraftKind::Alchemy;
            out.id            = std::string(row.Attribute("RecipeId"));
            out.name          = std::string(row.Attribute("UIName"));
            out.icon          = std::string(row.Attribute("UIIcon"));
            out.usage         = std::string(row.Attribute("UIUsage"));
            out.base          = std::string(row.Attribute("BaseMaterial"));
            out.perk          = Lower(row.Attribute("PerkId"));
            std::uint32_t dlc = 0;
            if (!ReadOptionalUnsigned(row.Attribute("DlcId"), 0, dlc) || dlc > 255 || !ReadOptionalUnsigned(row.Attribute("MinSkillLevel"), 0, out.minSkill) || !ReadUnsigned(row.Attribute("BaseQualityRewardXP"), out.baseXP)) {
                error = "a number of the recipe cannot be read";
                return false;
            }
            out.dlc = static_cast<std::uint8_t>(dlc);
            if (const Xml::Element *ingredients = row.Child("Ingredients")) {
                for (const Xml::Element &ingredient : ingredients->children) {
                    CustomRecipeMaterial material {Lower(ingredient.Attribute("IngredientItemId"))};
                    if (!ReadUnsigned(ingredient.Attribute("Quantity"), material.amount)) {
                        error = "an ingredient's Quantity cannot be read";
                        return false;
                    }
                    out.ingredients.push_back(std::move(material));
                }
            }
            if (const Xml::Element *products = row.Child("Products")) {
                for (const Xml::Element &entry : products->children) {
                    CustomRecipeProduct product {Lower(entry.Attribute("ProductItemId"))};
                    if (!ReadUnsigned(entry.Attribute("Quality"), product.threshold) || !ReadOptionalUnsigned(entry.Attribute("MaxYield"), 1, product.maxYield)) {
                        error = "a product's Quality or MaxYield cannot be read";
                        return false;
                    }
                    out.products.push_back(std::move(product));
                }
            }
            if (const Xml::Element *steps = row.Child("Steps")) {
                for (const Xml::Element &entry : steps->children) {
                    CustomAlchemyStep step;
                    step.type      = std::string(entry.Attribute("StepType"));
                    step.text      = std::string(entry.Attribute("UIText"));
                    step.condition = std::string(entry.Attribute("Condition"));
                    if (!ReadUnsigned(entry.Attribute("StepId"), step.id) || !ReadOptionalUnsigned(entry.Attribute("FeedbackPriority"), 0, step.feedbackPriority)) {
                        error = "a step's StepId or FeedbackPriority cannot be read";
                        return false;
                    }
                    out.steps.push_back(std::move(step));
                }
            }
            return true;
        }

        bool ReadSmithingRow(const Xml::Element &row, CustomRecipeDefinition &out, std::string &error) {
            out.kind      = CraftKind::Smithing;
            out.id        = std::string(row.Attribute("Id"));
            out.icon      = std::string(row.Attribute("UIIcon"));
            out.category  = std::string(row.Attribute("Category"));
            out.workpiece = std::string(row.Attribute("WorkpieceId"));
            out.perk      = Lower(row.Attribute("PerkId"));
            // The generator's choice: the localization key, else the text.
            if (const Xml::Element *name = row.Child("UIName")) {
                out.name = std::string(name->HasAttribute("StringName") ? name->Attribute("StringName") : name->Attribute("Text"));
            }
            if (const Xml::Element *description = row.Child("UIDesc")) {
                out.description = std::string(description->HasAttribute("StringName") ? description->Attribute("StringName") : description->Attribute("Text"));
            }
            std::uint32_t dlc = 0;
            if (!ReadOptionalUnsigned(row.Attribute("DlcId"), 0, dlc) || dlc > 255 || !ReadOptionalUnsigned(row.Attribute("MinSkillLevel"), 0, out.minSkill)) {
                error = "a number of the recipe cannot be read";
                return false;
            }
            out.dlc = static_cast<std::uint8_t>(dlc);
            if (const Xml::Element *ingredients = row.Child("Ingredients")) {
                for (const Xml::Element &ingredient : ingredients->children) {
                    CustomRecipeMaterial material {Lower(ingredient.Attribute("ItemClassId"))};
                    if (!ReadUnsigned(ingredient.Attribute("Amount"), material.amount)) {
                        error = "an ingredient's Amount cannot be read";
                        return false;
                    }
                    out.ingredients.push_back(std::move(material));
                }
            }
            if (const Xml::Element *products = row.Child("Products")) {
                for (const Xml::Element &entry : products->children) {
                    CustomRecipeProduct product {Lower(entry.Attribute("ItemClassId"))};
                    if (!ReadUnsigned(entry.Attribute("MinimalQuality"), product.threshold) || !ReadOptionalUnsigned(entry.Attribute("MaxYield"), 1, product.maxYield)) {
                        error = "a product's MinimalQuality or MaxYield cannot be read";
                        return false;
                    }
                    out.products.push_back(std::move(product));
                }
            }
            return true;
        }

        /** A whole number of zero or more, as JSON.stringify writes one or a C++ literal makes it. */
        bool IsCount(const nlohmann::json &value) {
            return value.is_number_unsigned() || (value.is_number_integer() && value.get<std::int64_t>() >= 0);
        }

        /** An optional picture: a base64 PNG by the rules a custom item's icon follows. */
        bool Picture(const nlohmann::json &value, const char *field, std::string &base64, std::string &png, std::string &error) {
            const auto found = value.find(field);
            if (found == value.end() || found->is_null()) {
                return true;
            }
            if (!found->is_string() || !KCDC::Shared::Image::ReadBase64Png(found->get_ref<const std::string &>(), png)) {
                error = std::string(field) + " must be a base64 PNG of at most 256 KiB and 4096x4096 pixels";
                return false;
            }
            base64 = found->get<std::string>();
            return true;
        }

        bool Text(const nlohmann::json &value, const char *field, bool required, std::string &out, std::string &error) {
            const auto found = value.find(field);
            if (found == value.end() || found->is_null()) {
                if (required) {
                    error = std::string(field) + " is required";
                }
                return !required;
            }
            if (!found->is_string() || found->get_ref<const std::string &>().size() > kMaximumText) {
                error = std::string(field) + " must be a string of at most 256 characters";
                return false;
            }
            out = found->get<std::string>();
            return true;
        }

        bool Unsigned(const nlohmann::json &value, const char *field, std::uint32_t fallback, std::uint32_t &out, std::string &error) {
            const auto found = value.find(field);
            if (found == value.end() || found->is_null()) {
                out = fallback;
                return true;
            }
            if (!IsCount(*found) || found->get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
                error = std::string(field) + " must be a non-negative integer";
                return false;
            }
            out = found->get<std::uint32_t>();
            return true;
        }

        template <typename Row>
        bool Rows(const nlohmann::json &value, const char *field, std::vector<Row> &out, std::string &error, const fu2::function<bool(const nlohmann::json &, Row &, std::string &) const> &read) {
            const auto found = value.find(field);
            if (found == value.end() || !found->is_array() || found->size() > kMaximumRows) {
                error = std::string(field) + " must be an array of at most 64 entries";
                return false;
            }
            for (const nlohmann::json &entry : *found) {
                Row row;
                if (!entry.is_object() || !read(entry, row, error)) {
                    if (error.empty()) {
                        error = std::string(field) + " entries must be objects";
                    }
                    error = std::string(field) + ": " + error;
                    return false;
                }
                out.push_back(std::move(row));
            }
            return true;
        }

        bool Item(const nlohmann::json &entry, std::string &out, std::string &error) {
            if (!Text(entry, "item", true, out, error)) {
                return false;
            }
            out = Lower(out);
            return true;
        }
    } // namespace

    bool AlchemyStepTypeXP(std::string_view type, std::uint32_t &xp) {
        constexpr std::array<std::pair<std::string_view, std::uint32_t>, 3> kTypes = {{{"Trivial", 0}, {"Easy", 12}, {"Hard", 24}}};
        for (const auto &[name, reward] : kTypes) {
            if (name == type) {
                xp = reward;
                return true;
            }
        }
        return false;
    }

    bool ParseAlchemyCondition(std::string_view condition, std::deque<std::string> &items, std::vector<AlchemyPredicate> &out, std::string &error) {
        if (condition.size() > kMaximumCondition) {
            error = "the condition is longer than 1024 characters";
            return false;
        }
        std::vector<AlchemyPredicate> predicates;
        for (const std::string_view term : Terms(Trim(condition))) {
            AlchemyPredicate predicate {};
            if (!ParseTerm(Trim(term), items, predicate)) {
                error = "unsupported alchemy condition: " + std::string(Trim(term));
                return false;
            }
            predicates.push_back(predicate);
        }
        out.insert(out.end(), predicates.begin(), predicates.end());
        return true;
    }

    bool ParseRecipeTable(std::string_view xml, CraftKind kind, std::vector<CustomRecipeDefinition> &out, std::vector<std::string> &problems, std::string &error) {
        Xml::Element root;
        if (!Xml::Parse(xml, root, error)) {
            return false;
        }
        const char *table           = kind == CraftKind::Alchemy ? "AlchemyRecipes" : "BlacksmithRecipes";
        const char *element         = kind == CraftKind::Alchemy ? "AlchemyRecipe" : "BlacksmithRecipe";
        const Xml::Element *recipes = root.tag == table ? &root : root.Child(table);
        if (recipes == nullptr) {
            error = std::string("no <") + table + "> element";
            return false;
        }
        for (const Xml::Element &row : recipes->children) {
            if (row.tag != element) {
                continue;
            }
            CustomRecipeDefinition definition;
            std::string reason;
            if (!(kind == CraftKind::Alchemy ? ReadAlchemyRow(row, definition, reason) : ReadSmithingRow(row, definition, reason))) {
                problems.push_back("recipe '" + definition.id + "': " + reason);
                continue;
            }
            out.push_back(std::move(definition));
        }
        return true;
    }

    bool CustomRecipeFromJson(const nlohmann::json &value, CustomRecipeDefinition &out, std::string &error) {
        if (!value.is_object()) {
            error = "the definition must be an object";
            return false;
        }
        std::string kind;
        if (!Text(value, "kind", true, kind, error)) {
            return false;
        }
        if (kind != "alchemy" && kind != "smithing") {
            error = "kind must be 'alchemy' or 'smithing'";
            return false;
        }
        CustomRecipeDefinition definition;
        definition.kind      = kind == "alchemy" ? CraftKind::Alchemy : CraftKind::Smithing;
        const bool alchemy   = definition.kind == CraftKind::Alchemy;
        const auto id        = value.find("id");
        const bool numericId = id != value.end() && IsCount(*id);
        if (alchemy ? !numericId : (id == value.end() || !id->is_string())) {
            error = alchemy ? "an alchemy recipe's id must be a non-negative integer" : "a smithing recipe's id must be a string";
            return false;
        }
        definition.id = alchemy ? std::to_string(id->get<std::uint64_t>()) : id->get<std::string>();

        std::uint32_t dlc = 0;
        if (!Text(value, "name", true, definition.name, error) || !Text(value, "icon", false, definition.icon, error) || !Unsigned(value, "dlcId", 0, dlc, error)) {
            return false;
        }
        if (dlc > kMaxDLCId) {
            error = "dlcId names no DLC";
            return false;
        }
        definition.dlc = static_cast<std::uint8_t>(dlc);
        if (!Picture(value, "iconImage", definition.iconImage, definition.iconPng, error)) {
            return false;
        }
        if (!definition.iconImage.empty() && !definition.icon.empty()) {
            error = "icon names one of the game's icons and iconImage brings one; give one of them";
            return false;
        }

        const auto material = [](const nlohmann::json &entry, CustomRecipeMaterial &row, std::string &error) {
            return Item(entry, row.item, error) && Unsigned(entry, "amount", 0, row.amount, error);
        };
        const auto product = [](const nlohmann::json &entry, CustomRecipeProduct &row, std::string &error) {
            return Item(entry, row.item, error) && Unsigned(entry, "threshold", 0, row.threshold, error) && Unsigned(entry, "maxYield", 1, row.maxYield, error);
        };
        if (!Rows<CustomRecipeMaterial>(value, "ingredients", definition.ingredients, error, material) || !Rows<CustomRecipeProduct>(value, "products", definition.products, error, product)) {
            return false;
        }

        if (alchemy) {
            if (value.contains("perk") || value.contains("minSkill") || value.contains("category") || value.contains("workpiece") || value.contains("description")) {
                error = "perk, minSkill, category, workpiece and description are smithing fields";
                return false;
            }
            const auto step = [](const nlohmann::json &entry, CustomAlchemyStep &row, std::string &error) {
                return Unsigned(entry, "id", 0, row.id, error) && Text(entry, "type", true, row.type, error) && Unsigned(entry, "feedbackPriority", 0, row.feedbackPriority, error) && Text(entry, "text", false, row.text, error)
                       && Text(entry, "condition", true, row.condition, error);
            };
            if (!Text(value, "base", true, definition.base, error) || !Text(value, "usage", false, definition.usage, error) || !Unsigned(value, "baseXP", 0, definition.baseXP, error) || !Rows<CustomAlchemyStep>(value, "steps", definition.steps, error, step)
                || !Picture(value, "productImage", definition.productImage, definition.productPng, error) || !Picture(value, "recipeImage", definition.recipeImage, definition.recipePng, error)) {
                return false;
            }
            if (const auto borrowed = value.find("bookPicturesFrom"); borrowed != value.end() && !borrowed->is_null()) {
                if (!IsCount(*borrowed) || borrowed->get<std::uint64_t>() == 0 || borrowed->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
                    error = "bookPicturesFrom must be the id of one of the game's alchemy recipes";
                    return false;
                }
                if (!definition.productImage.empty() || !definition.recipeImage.empty()) {
                    error = "bookPicturesFrom brings both book pictures; leave productImage and recipeImage out";
                    return false;
                }
                definition.bookPicturesFrom = std::to_string(borrowed->get<std::uint64_t>());
            }
        }
        else {
            if (value.contains("base") || value.contains("usage") || value.contains("baseXP") || value.contains("steps") || value.contains("productImage") || value.contains("recipeImage") || value.contains("bookPicturesFrom")) {
                error = "base, usage, baseXP, steps, productImage, recipeImage and bookPicturesFrom are alchemy fields";
                return false;
            }
            if (!Text(value, "perk", false, definition.perk, error) || !Unsigned(value, "minSkill", 0, definition.minSkill, error) || !Text(value, "category", true, definition.category, error) || !Text(value, "workpiece", true, definition.workpiece, error)
                || !Text(value, "description", false, definition.description, error)) {
                return false;
            }
            definition.perk = Lower(definition.perk);
            // A script's recipe reaches the client's own tables through the kinds they already hold.
            if (!In(kSmithingCategories, definition.category)) {
                error = "category must be ShortSword, LongSword, Axe or Other";
                return false;
            }
            if (!In(kWorkpieces, definition.workpiece)) {
                error = "workpiece must be sword, longsword, axe or horseshoe";
                return false;
            }
        }
        if (definition.iconPng.size() + definition.productPng.size() + definition.recipePng.size() > kMaximumRecipeImageBytes) {
            error = "a recipe's pictures take at most 256 KiB of PNG together";
            return false;
        }
        out = std::move(definition);
        return true;
    }

    std::string RecipeIconId(const CustomRecipeDefinition &definition) {
        return definition.iconPng.empty() ? std::string() : "kcdc_ri_" + Framework::Utils::Crypto::Sha256Hex(definition.iconPng).substr(0, 16);
    }

    nlohmann::json CustomRecipeToJson(const CustomRecipeDefinition &definition) {
        const bool alchemy    = definition.kind == CraftKind::Alchemy;
        nlohmann::json json   = {{"kind", alchemy ? "alchemy" : "smithing"}, {"name", definition.name}, {"icon", definition.icon}, {"dlcId", definition.dlc}};
        std::uint32_t numeric = 0;
        if (alchemy && ReadUnsigned(definition.id, numeric)) {
            json["id"] = numeric;
        }
        else {
            json["id"] = definition.id;
        }
        json["ingredients"] = nlohmann::json::array();
        for (const auto &ingredient : definition.ingredients) {
            json["ingredients"].push_back({{"item", ingredient.item}, {"amount", ingredient.amount}});
        }
        json["products"] = nlohmann::json::array();
        for (const auto &product : definition.products) {
            json["products"].push_back({{"item", product.item}, {"threshold", product.threshold}, {"maxYield", product.maxYield}});
        }
        const std::pair<const char *, const std::string *> pictures[] = {{"iconImage", &definition.iconImage}, {"productImage", &definition.productImage}, {"recipeImage", &definition.recipeImage}};
        for (const auto &[field, picture] : pictures) {
            if (!picture->empty()) {
                json[field] = *picture;
            }
        }
        if (!definition.bookPicturesFrom.empty()) {
            json["bookPicturesFrom"] = std::stoul(definition.bookPicturesFrom);
        }
        if (alchemy) {
            json["base"]   = definition.base;
            json["usage"]  = definition.usage;
            json["baseXP"] = definition.baseXP;
            json["steps"]  = nlohmann::json::array();
            for (const auto &step : definition.steps) {
                json["steps"].push_back({{"id", step.id}, {"type", step.type}, {"feedbackPriority", step.feedbackPriority}, {"text", step.text}, {"condition", step.condition}});
            }
        }
        else {
            json["perk"]        = definition.perk;
            json["minSkill"]    = definition.minSkill;
            json["category"]    = definition.category;
            json["workpiece"]   = definition.workpiece;
            json["description"] = definition.description;
        }
        return json;
    }

    std::shared_ptr<const CustomRecipe> CustomRecipe::Compile(CustomRecipeDefinition definition, std::string &error) {
        const bool alchemy    = definition.kind == CraftKind::Alchemy;
        std::uint32_t numeric = 0;
        // The game keys an alchemy row by a signed int (FindRecipeById 0x1808C3C0C); a smithing id
        // travels in SmithingBegin.
        const bool numericId = ReadUnsigned(definition.id, numeric) && numeric != 0 && numeric <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) && std::to_string(numeric) == definition.id;
        if (alchemy ? !numericId : definition.id.empty() || definition.id.size() > RPC::kMaxSmithingRecipeId) {
            error = alchemy ? "an alchemy recipe's id must be a whole number from 1 to 2147483647" : "a smithing recipe's id must be 1 to 128 characters";
            return nullptr;
        }
        if (!alchemy && !std::all_of(definition.id.begin(), definition.id.end(), [](unsigned char c) {
                return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.';
            })) {
            error = "a smithing recipe's id takes letters, digits, '_', '-' and '.' only";
            return nullptr;
        }
        if (definition.name.empty()) {
            error = "the recipe needs a name";
            return nullptr;
        }
        if (!definition.perk.empty() && !RPC::IsItemClassGuidText(definition.perk)) {
            error = "perk must be a perk GUID";
            return nullptr;
        }

        // As the crafting generator checks the shipped rows.
        std::set<std::string_view> seen;
        if (definition.ingredients.empty() || definition.ingredients.size() > kMaximumRows) {
            error = "the recipe needs 1 to 64 ingredients";
            return nullptr;
        }
        for (const auto &ingredient : definition.ingredients) {
            if (!RPC::IsItemClassGuidText(ingredient.item) || ingredient.amount == 0 || !seen.insert(ingredient.item).second) {
                error = "ingredient " + ingredient.item + " must be an item class GUID, named once, with a positive amount";
                return nullptr;
            }
        }
        if (definition.products.empty() || definition.products.size() > kMaximumRows) {
            error = "the recipe needs 1 to 64 products";
            return nullptr;
        }
        std::set<std::uint32_t> thresholds;
        for (const auto &product : definition.products) {
            const bool threshold = alchemy ? product.threshold >= 1 && product.threshold <= 4 : product.threshold <= 100;
            const bool yield     = alchemy ? product.maxYield >= 1 && product.maxYield <= kMaximumAlchemyYield : product.maxYield >= 1;
            if (!RPC::IsItemClassGuidText(product.item) || !threshold || !yield || !thresholds.insert(product.threshold).second) {
                error = alchemy ? "product " + product.item + " must be an item class GUID with its own grade, 1 to 4, and a yield of 1 to 64" : "product " + product.item + " must be an item class GUID with its own minimal quality, 0 to 100";
                return nullptr;
            }
        }

        auto recipe         = std::shared_ptr<CustomRecipe>(new CustomRecipe());
        recipe->_definition = std::move(definition);
        const auto &stored  = recipe->_definition;
        if (alchemy) {
            if (!In(kPotionBases, stored.base)) {
                error = "base must be Water, Wine, Oil or Spiritus";
                return nullptr;
            }
            if (stored.steps.empty() || stored.steps.size() > kMaximumAlchemyStep) {
                error = "the recipe needs 1 to 28 steps; the first identifies it";
                return nullptr;
            }
            // The predicates are all read before a step views them, so the vector holds still.
            std::vector<std::pair<std::size_t, std::size_t>> ranges;
            std::set<std::uint32_t> ids;
            for (const auto &step : stored.steps) {
                std::uint32_t xp = 0;
                if (step.id < 1 || step.id > kMaximumAlchemyStep || !ids.insert(step.id).second) {
                    error = "step ids must be distinct, 1 to 28";
                    return nullptr;
                }
                if (!AlchemyStepTypeXP(step.type, xp)) {
                    error = "step " + std::to_string(step.id) + ": type must be Trivial, Easy or Hard";
                    return nullptr;
                }
                const std::size_t first = recipe->_predicates.size();
                if (!ParseAlchemyCondition(step.condition, recipe->_predicateItems, recipe->_predicates, error)) {
                    error = "step " + std::to_string(step.id) + ": " + error;
                    return nullptr;
                }
                ranges.emplace_back(first, recipe->_predicates.size() - first);
            }
            for (std::size_t index = 0; index < stored.steps.size(); ++index) {
                const auto &step = stored.steps[index];
                std::uint32_t xp = 0;
                AlchemyStepTypeXP(step.type, xp);
                recipe->_steps.push_back({step.id, xp, step.feedbackPriority, step.text, std::span<const AlchemyPredicate>(recipe->_predicates.data() + ranges[index].first, ranges[index].second)});
            }
            recipe->_program = {stored.id, stored.baseXP, recipe->_steps};
        }

        recipe->_recipe = {stored.kind, stored.id, stored.name, stored.dlc, stored.minSkill, stored.perk, stored.base, {}, {}};
        for (const auto &ingredient : stored.ingredients) {
            recipe->_recipe.ingredients.push_back({ingredient.item, ingredient.amount});
        }
        for (const auto &product : stored.products) {
            recipe->_recipe.products.push_back({product.item, product.threshold, product.maxYield});
        }
        return recipe;
    }
} // namespace KCDC::Shared::Game
