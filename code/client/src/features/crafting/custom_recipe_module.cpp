#include <utils/safe_win32.h>

#include "custom_recipe_module.h"

#include "features/ui/ui_image_module.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/patterns.h"
#include "sdk/wh/framework/i_ui_book.h"
#include "sdk/wh/playermodule/c_alchemy_recipe_database.h"
#include "sdk/wh/playermodule/c_blacksmith_recipe_database.h"
#include "shared/features/crafting/custom_recipe_message.h"

#include <MinHook.h>
#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <nlohmann/json.hpp>
#include <utils/hooking/hook_function.h>

#include <charconv>
#include <string_view>

namespace KCDC::Features::Crafting {
    namespace {
        constexpr std::string_view kLabelPrefix = "kcdc_rc_";
        // Bodies queued while the game thread is busy loading; far more than a server sends.
        constexpr std::size_t kMaximumIncoming = 1024;

        /** A one-row document of `table`, with the row returned through `row`. */
        SDK::XmlNodeRef Document(const char *table, const char *element, SDK::XmlNodeRef &row) {
            const std::string xml = std::string("<database><") + table + " version=\"1\"><" + element + "/></" + table + "></database>";
            SDK::XmlNodeRef root  = SDK::ISystem::GetInstance()->LoadXmlFromBuffer(xml.c_str(), xml.size(), false, false);
            if (root.IsValid()) {
                row = root->getChild(0)->getChild(0);
            }
            return root;
        }

        /** An attribute left out keeps the row's default, as a stock row that omits it does. */
        void Set(const SDK::XmlNodeRef &node, const char *key, const std::string &value) {
            if (!value.empty()) {
                node->setAttr(key, value.c_str());
            }
        }
    } // namespace

    CustomRecipeModule &CustomRecipeModule::Get() {
        static CustomRecipeModule instance;
        return instance;
    }

    void CustomRecipeModule::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::CustomRecipeDefinitions>([](const KCDC::Shared::RPC::CustomRecipeDefinitions &message, MafiaNet::Packet *) {
            if (message.valid) {
                Get().Receive(message.body);
            }
        });
    }

    void CustomRecipeModule::Receive(const std::string &body) {
        std::scoped_lock lock(_mutex);
        if (_incoming.size() < kMaximumIncoming) {
            _incoming.push_back(body);
        }
    }

    void CustomRecipeModule::Update() {
        std::vector<std::string> incoming;
        {
            std::scoped_lock lock(_mutex);
            incoming.swap(_incoming);
        }
        for (const std::string &body : incoming) {
            const nlohmann::json message = nlohmann::json::parse(body, nullptr, false);
            if (!message.is_object() || !message.contains("from") || !message.at("from").is_number_unsigned() || !message.contains("recipes") || !message.at("recipes").is_array()) {
                continue;
            }
            // The list only grows, in order, so an entry is either one this session holds or the next.
            // A server sends its list from the start once per connection, so a list that starts again
            // is a new server's: one left queued by a join that never reached the session is dropped.
            std::size_t index = message.at("from").get<std::size_t>();
            if (index == 0) {
                if (_appended != 0) {
                    _stale = true;
                }
                _definitions.clear();
                _appended = 0;
                for (const std::string &path : _pictures) {
                    KCDC::Features::UI::UiImageModule::Get().Withdraw(path);
                }
                _pictures.clear();
                std::scoped_lock lock(_mutex);
                _bookPictures.clear();
            }
            for (const nlohmann::json &entry : message.at("recipes")) {
                if (index++ != _definitions.size()) {
                    continue;
                }
                KCDC::Shared::Game::CustomRecipeDefinition definition;
                std::string error;
                if (!KCDC::Shared::Game::CustomRecipeFromJson(entry, definition, error) || KCDC::Shared::Game::CustomRecipe::Compile(definition, error) == nullptr) {
                    // Kept in its place, so the indices stay the server's; its row is never appended.
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Custom recipe from the server refused: {}", error);
                    definition.id.clear();
                }
                _definitions.push_back(std::move(definition));
            }
        }

        // An ended session's rows still hold their ids until the level edge reads the tables again.
        if (_stale) {
            return;
        }
        std::size_t appended = 0;
        for (; _appended < _definitions.size(); ++_appended) {
            const auto &definition = _definitions[_appended];
            if (definition.id.empty()) {
                continue;
            }
            if (!Append(definition, _appended)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("The game's {} table refused custom recipe {}", definition.kind == KCDC::Shared::Game::CraftKind::Alchemy ? "alchemy" : "smithing", definition.id);
                continue;
            }
            _rowsInTables = true;
            ++appended;
        }
        if (appended != 0) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("{} custom recipe(s) added to the game's recipe tables", appended);
        }
    }

    std::string CustomRecipeModule::Label(std::size_t index, const char *field, const std::string &text) {
        if (text.empty()) {
            return {};
        }
        const std::string key = std::string(kLabelPrefix) + std::to_string(index) + "_" + field;
        std::scoped_lock lock(_mutex);
        _labels.insert_or_assign(key, text);
        return "@" + key;
    }

    void CustomRecipeModule::ServePicture(const KCDC::Shared::Game::CustomRecipeDefinition &definition, const std::string &png, const std::string &path, std::uint32_t side) {
        if (png.empty() || _pictures.contains(path)) {
            return;
        }
        auto pixels = KCDC::Features::UI::UiImageModule::Decode(png, side);
        if (pixels == nullptr) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Custom recipe {}: a picture is not a PNG this client can decode; the game's placeholder shows instead", definition.id);
            return;
        }
        KCDC::Features::UI::UiImageModule::Get().Serve(path, std::move(pixels));
        _pictures.insert(path);
    }

    bool CustomRecipeModule::Append(const KCDC::Shared::Game::CustomRecipeDefinition &definition, std::size_t index) {
        // Served before the row exists, so no screen ever asks for one of them in vain.
        const std::string iconId = KCDC::Shared::Game::RecipeIconId(definition);
        if (!iconId.empty()) {
            ServePicture(definition, definition.iconPng, "Libs/UI/Textures/Icons/Items/" + iconId + "_icon.dds", KCDC::Shared::Game::kRecipeIconSide);
        }
        const std::string icon = iconId.empty() ? definition.icon : iconId;
        SDK::XmlNodeRef row;
        if (definition.kind == KCDC::Shared::Game::CraftKind::Alchemy) {
            auto *database   = SDK::wh::playermodule::C_AlchemyRecipeDatabase::GetInstance();
            std::int32_t id  = 0;
            const auto parse = std::from_chars(definition.id.data(), definition.id.data() + definition.id.size(), id);
            // A duplicate key is appended beside the row it duplicates, so a taken id is refused first.
            if (parse.ec != std::errc() || database->FindRecipe(id) != nullptr) {
                return false;
            }
            const SDK::XmlNodeRef root = Document("AlchemyRecipes", "AlchemyRecipe", row);
            if (!root.IsValid() || !row.IsValid()) {
                return false;
            }
            Set(row, "RecipeId", definition.id);
            Set(row, "BaseMaterial", definition.base);
            Set(row, "UIName", Label(index, "n", definition.name));
            Set(row, "UIUsage", Label(index, "u", definition.usage));
            Set(row, "UIIcon", icon);
            Set(row, "BaseQualityRewardXP", std::to_string(definition.baseXP));
            ServePicture(definition, definition.productPng, "Libs/UI/Textures/Books/Products/" + definition.id + "_ui.dds", KCDC::Shared::Game::kRecipeProductImageSide);
            ServePicture(definition, definition.recipePng, "Libs/UI/Textures/Books/Recipes/" + definition.id + "_ui.dds", KCDC::Shared::Game::kRecipeImageSide);
            if (!definition.bookPicturesFrom.empty()) {
                std::scoped_lock lock(_mutex);
                _bookPictures.insert_or_assign(definition.id, definition.bookPicturesFrom);
            }
            Set(row, "DlcId", std::to_string(definition.dlc));
            const SDK::XmlNodeRef ingredients = row->newChild("Ingredients");
            for (const auto &ingredient : definition.ingredients) {
                const SDK::XmlNodeRef node = ingredients->newChild("AlchemyIngredient");
                Set(node, "IngredientItemId", ingredient.item);
                Set(node, "Quantity", std::to_string(ingredient.amount));
            }
            const SDK::XmlNodeRef products = row->newChild("Products");
            for (const auto &product : definition.products) {
                const SDK::XmlNodeRef node = products->newChild("AlchemyProduct");
                Set(node, "ProductItemId", product.item);
                Set(node, "Quality", std::to_string(product.threshold));
                Set(node, "MaxYield", std::to_string(product.maxYield));
            }
            const SDK::XmlNodeRef steps = row->newChild("Steps");
            for (const auto &step : definition.steps) {
                const SDK::XmlNodeRef node = steps->newChild("AlchemyRecipeStep");
                Set(node, "StepId", std::to_string(step.id));
                Set(node, "StepType", step.type);
                Set(node, "FeedbackPriority", std::to_string(step.feedbackPriority));
                Set(node, "UIText", Label(index, ("s" + std::to_string(step.id)).c_str(), step.text));
                Set(node, "Condition", step.condition);
            }
            return database->AppendFromXml(root, nullptr, false) && database->RebuildIndex() && database->FindRecipe(id) != nullptr;
        }

        auto *database = SDK::wh::playermodule::C_BlacksmithRecipeDatabase::GetInstance();
        if (database->FindRecipe(definition.id.c_str()) != nullptr) {
            return false;
        }
        const SDK::XmlNodeRef root = Document("BlacksmithRecipes", "BlacksmithRecipe", row);
        if (!root.IsValid() || !row.IsValid()) {
            return false;
        }
        Set(row, "Id", definition.id);
        Set(row, "UIIcon", icon);
        Set(row, "Category", definition.category);
        Set(row, "MinSkillLevel", std::to_string(definition.minSkill));
        // The setter resolves it to a BlacksmithWorkpieces row; the definition names one of the four.
        Set(row, "WorkpieceId", definition.workpiece);
        if (!definition.perk.empty()) {
            Set(row, "PerkId", definition.perk);
        }
        Set(row, "DlcId", std::to_string(definition.dlc));
        Set(row->newChild("UIName"), "StringName", Label(index, "n", definition.name));
        if (!definition.description.empty()) {
            Set(row->newChild("UIDesc"), "StringName", Label(index, "d", definition.description));
        }
        const SDK::XmlNodeRef ingredients = row->newChild("Ingredients");
        for (const auto &ingredient : definition.ingredients) {
            const SDK::XmlNodeRef node = ingredients->newChild("BlacksmithRecipeIngredient");
            Set(node, "ItemClassId", ingredient.item);
            Set(node, "Amount", std::to_string(ingredient.amount));
        }
        const SDK::XmlNodeRef products = row->newChild("Products");
        for (const auto &product : definition.products) {
            const SDK::XmlNodeRef node = products->newChild("BlacksmithRecipeProduct");
            Set(node, "ItemClassId", product.item);
            Set(node, "MinimalQuality", std::to_string(product.threshold));
        }
        return database->AppendFromXml(root, nullptr, false) && database->RebuildIndex() && database->FindRecipe(definition.id.c_str()) != nullptr;
    }

    void CustomRecipeModule::Reset() {
        // The rows stay in the tables, where the next session would find them under its own ids.
        if (_rowsInTables) {
            _stale = true;
        }
        _appended = 0;
        _definitions.clear();
        // The next server's recipes may use the same ids with other pictures, or none.
        for (const std::string &path : _pictures) {
            KCDC::Features::UI::UiImageModule::Get().Withdraw(path);
        }
        _pictures.clear();
        std::scoped_lock lock(_mutex);
        _bookPictures.clear();
        _incoming.clear();
        _labels.clear();
    }

    void CustomRecipeModule::OnRecipeTablesReloaded() {
        // Whatever this process appended is gone; the next tick appends this session's again.
        _rowsInTables = false;
        _stale        = false;
        _appended     = 0;
    }

    bool CustomRecipeModule::WantsReload() const {
        return _stale;
    }

    std::string CustomRecipeModule::BookPicturesOf(std::string_view recipeId) const {
        std::scoped_lock lock(_mutex);
        const auto found = _bookPictures.find(recipeId);
        return found != _bookPictures.end() ? found->second : std::string(recipeId);
    }

    bool CustomRecipeModule::ResolveLabel(const char *label, SDK::CryStringT<char> &out) const {
        std::string_view key(label);
        if (key.starts_with('@')) {
            key.remove_prefix(1);
        }
        if (!key.starts_with(kLabelPrefix)) {
            return false;
        }
        std::scoped_lock lock(_mutex);
        const auto found = _labels.find(key);
        if (found == _labels.end()) {
            return false;
        }
        out.Assign(found->second.c_str());
        return true;
    }

    namespace {
        void(__fastcall *g_setRecipeOriginal)(SDK::wh::framework::I_UIBook *, const SDK::CryStringT<char> &, const SDK::CryStringT<char> &, const SDK::CryStringT<char> &, const void *, bool, float) = nullptr;

        // The book builds a page's two pictures from the id alone, while its name, usage and steps
        // arrive on their own: a recipe that borrows a game recipe's pictures hands that id instead.
        void __fastcall SetRecipe(SDK::wh::framework::I_UIBook *book, const SDK::CryStringT<char> &id, const SDK::CryStringT<char> &name, const SDK::CryStringT<char> &usage, const void *steps, bool isOnRight, float dyslexiaLevel) {
            const std::string own      = id.m_pData != nullptr ? id.m_pData : "";
            const std::string pictures = CustomRecipeModule::Get().BookPicturesOf(own);
            if (pictures == own) {
                g_setRecipeOriginal(book, id, name, usage, steps, isOnRight, dyslexiaLevel);
                return;
            }
            const SDK::ScopedCryString borrowed(pictures.c_str());
            g_setRecipeOriginal(book, borrowed.Get(), name, usage, steps, isOnRight, dyslexiaLevel);
        }
    } // namespace

    void CustomRecipeModule::InstallHooks() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIBook__SetRecipe), reinterpret_cast<LPVOID>(&SetRecipe), reinterpret_cast<LPVOID *>(&g_setRecipeOriginal));
    }
} // namespace KCDC::Features::Crafting

static InitFunction init(
    [] {
        KCDC::Features::Crafting::CustomRecipeModule::InstallHooks();
    },
    "Custom recipe pictures");
