#include "mod_table_service.h"

#include "shared/game/custom_recipe_registry.h"
#include "shared/game/mod_item_tables.h"
#include "shared/streamed_assets/streamed_asset_policy.h"

#include <logging/logger.h>
#include <scripting/resource/resource.h>
#include <scripting/resource/resource_manager.h>
#include <utils/streamed_assets/policy.h>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace KCDC::Features::StreamedAssets {
    namespace {
        constexpr const char *kLogger = "KCDCServer";

        bool IsItemTablePatch(std::string_view entry) {
            return KCDC::Shared::StreamedAssets::Policy::IsTablePatch(entry) && entry.rfind("libs/tables/item/item", 0) == 0;
        }

        bool IsClothingPatch(std::string_view entry) {
            return KCDC::Shared::StreamedAssets::Policy::IsTablePatch(entry) && entry.rfind("libs/tables/character/charactercomponent", 0) == 0;
        }

        /** The recipe table a patch extends: AlchemyRecipe.xml or BlacksmithRecipes.xml, and not AlchemyRecipeStepType.xml. */
        std::optional<KCDC::Shared::Game::CraftKind> RecipePatchKind(std::string_view entry) {
            if (!KCDC::Shared::StreamedAssets::Policy::IsTablePatch(entry)) {
                return std::nullopt;
            }
            if (entry.rfind("libs/tables/minigame/alchemyrecipe__", 0) == 0) {
                return KCDC::Shared::Game::CraftKind::Alchemy;
            }
            if (entry.rfind("libs/tables/minigame/blacksmithrecipes__", 0) == 0) {
                return KCDC::Shared::Game::CraftKind::Smithing;
            }
            return std::nullopt;
        }

        std::string ReadFile(const std::filesystem::path &file) {
            std::ifstream stream(file, std::ios::binary);
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }
    } // namespace

    void ModTableService::Install(Framework::Scripting::ResourceManager &resources) {
        if (_installed) {
            return;
        }
        _installed = true;
        resources.AddOnResourceStarted([this, &resources](const std::string &name) {
            const Framework::Scripting::Resource *resource = resources.GetResource(name);
            if (resource != nullptr) {
                Register(name, resource->GetPath());
            }
        });
        resources.AddOnResourceStopped([this](const std::string &name) { Unregister(name); });
    }

    std::size_t ModTableService::Register(const std::string &resource, const std::filesystem::path &root) {
        const std::filesystem::path laneRoot = root / KCDC::Shared::StreamedAssets::kLaneReplace;
        std::error_code error;
        if (!std::filesystem::is_directory(laneRoot, error) || !Framework::Utils::StreamedAssets::IsValidResourceName(resource)) {
            Unregister(resource);
            return 0;
        }

        // The catalog holds what clients are sent, so the lane is walked the way the asset pak
        // builder packs it: each file normalised and checked by the same policy, and a file the
        // policy refuses keeps the whole lane from shipping -- and its classes from the catalog.
        const KCDC::Shared::StreamedAssets::Policy &policy = *KCDC::Shared::StreamedAssets::Policy::Get();
        std::vector<std::pair<std::string, std::filesystem::path>> itemTables;
        std::vector<std::pair<std::string, std::filesystem::path>> clothingTables;
        std::vector<std::pair<std::string, std::filesystem::path>> recipeTables;
        std::filesystem::recursive_directory_iterator walk(laneRoot, std::filesystem::directory_options::skip_permission_denied, error);
        for (; !error && walk != std::filesystem::recursive_directory_iterator(); walk.increment(error)) {
            // Its own code: the walk's carries the iteration's state, and the next step resets it.
            std::error_code entryError;
            if (!walk->is_regular_file(entryError)) {
                continue;
            }
            const std::string relative = std::filesystem::relative(walk->path(), laneRoot, entryError).generic_string();
            if (entryError) {
                continue;
            }
            std::string normalized;
            std::string reason;
            if (!policy.NormalizeEntry(KCDC::Shared::StreamedAssets::kLaneReplace, resource, relative, normalized, reason)) {
                Framework::Logging::GetLogger(kLogger)->warn("Resource '{}': {} keeps its replace lane from shipping ({}), so none of its item classes join the catalog", resource, relative, reason);
                Unregister(resource);
                return 0;
            }
            if (!policy.ShouldShip(KCDC::Shared::StreamedAssets::kLaneReplace, resource, normalized, walk->path().string(), reason)) {
                continue;
            }
            if (IsItemTablePatch(normalized)) {
                itemTables.emplace_back(normalized, walk->path());
            }
            else if (IsClothingPatch(normalized)) {
                clothingTables.emplace_back(normalized, walk->path());
            }
            else if (RecipePatchKind(normalized)) {
                recipeTables.emplace_back(normalized, walk->path());
            }
        }
        if (error) {
            Framework::Logging::GetLogger(kLogger)->warn("Resource '{}': its replace lane could not be read ({}), so none of its item classes join the catalog", resource, error.message());
            Unregister(resource);
            return 0;
        }

        // In the pak's own entry order, which is the order a client reads them in.
        std::sort(itemTables.begin(), itemTables.end());
        std::sort(clothingTables.begin(), clothingTables.end());
        std::sort(recipeTables.begin(), recipeTables.end());
        const auto readable = [&resource](const std::pair<std::string, std::filesystem::path> &table) {
            std::error_code sizeError;
            const std::uintmax_t size = std::filesystem::file_size(table.second, sizeError);
            if (sizeError || size > KCDC::Shared::Game::kMaximumModTableBytes) {
                Framework::Logging::GetLogger(kLogger)->warn("Resource '{}': {} is too large to read", resource, table.first);
                return false;
            }
            return true;
        };

        KCDC::Shared::Game::ModClothingTypes clothing;
        for (const auto &table : clothingTables) {
            std::string reason;
            if (readable(table) && !KCDC::Shared::Game::ParseModClothingTypes(ReadFile(table.second), clothing, reason)) {
                Framework::Logging::GetLogger(kLogger)->warn("Resource '{}': {} is not a character component table ({})", resource, table.first, reason);
            }
        }
        std::vector<KCDC::Shared::Game::ModItemClass> items;
        std::vector<std::string> paths;
        for (const auto &table : itemTables) {
            std::string reason;
            if (!readable(table)) {
                continue;
            }
            if (!KCDC::Shared::Game::ParseModItemTable(ReadFile(table.second), clothing, items, reason)) {
                Framework::Logging::GetLogger(kLogger)->warn("Resource '{}': {} is not an item table ({})", resource, table.first, reason);
                continue;
            }
            paths.push_back(table.first);
        }
        const std::size_t count  = items.size();
        const std::size_t tables = paths.size();
        KCDC::Shared::Game::ModItemRegistry::Get().Replace(resource, std::move(items));
        if (tables != 0) {
            Framework::Logging::GetLogger(kLogger)->info("Resource '{}': {} item class(es) from {} table patch(es) join the catalog", resource, count, tables);
        }

        // After the items: a recipe may name a class the same mod adds.
        std::vector<std::shared_ptr<const KCDC::Shared::Game::CustomRecipe>> recipes;
        for (const auto &table : recipeTables) {
            std::vector<KCDC::Shared::Game::CustomRecipeDefinition> rows;
            std::vector<std::string> problems;
            std::string reason;
            if (!readable(table)) {
                continue;
            }
            if (!KCDC::Shared::Game::ParseRecipeTable(ReadFile(table.second), *RecipePatchKind(table.first), rows, problems, reason)) {
                Framework::Logging::GetLogger(kLogger)->warn("Resource '{}': {} is not a recipe table ({})", resource, table.first, reason);
                continue;
            }
            for (auto &row : rows) {
                const std::string id = row.id;
                if (auto recipe = KCDC::Shared::Game::CustomRecipe::Compile(std::move(row), reason)) {
                    recipes.push_back(std::move(recipe));
                }
                else {
                    problems.push_back("recipe '" + id + "': " + reason);
                }
            }
            for (const std::string &problem : problems) {
                Framework::Logging::GetLogger(kLogger)->warn("Resource '{}': {}: {}; the server will not craft it", resource, table.first, problem);
            }
            paths.push_back(table.first);
        }
        if (recipes.empty()) {
            KCDC::Shared::Game::CustomRecipeRegistry::Get().RemoveTables(resource);
        }
        else {
            const std::size_t offered              = recipes.size();
            const std::vector<std::string> refused = KCDC::Shared::Game::CustomRecipeRegistry::Get().ReplaceTables(resource, std::move(recipes));
            for (const std::string &problem : refused) {
                Framework::Logging::GetLogger(kLogger)->warn("Resource '{}': {}; the server will not craft it", resource, problem);
            }
            Framework::Logging::GetLogger(kLogger)->info("Resource '{}': {} recipe(s) from its table patches join crafting", resource, offered - refused.size());
        }
        _tables[resource] = std::move(paths);
        _unverified.insert(resource);
        return count;
    }

    void ModTableService::Unregister(const std::string &resource) {
        KCDC::Shared::Game::CustomRecipeRegistry::Get().RemoveTables(resource);
        KCDC::Shared::Game::ModItemRegistry::Get().Remove(resource);
        _tables.erase(resource);
        _unverified.erase(resource);
    }

    void ModTableService::VerifyShipped(const Framework::Integrations::Server::AssetPakBuilder &paks) {
        for (const std::string &resource : _unverified) {
            const auto registered = _tables.find(resource);
            if (registered == _tables.end() || registered->second.empty()) {
                continue;
            }
            const bool shipped = paks.IsActive() && std::all_of(registered->second.begin(), registered->second.end(), [&paks](const std::string &path) { return paks.HasEntry(KCDC::Shared::StreamedAssets::kLaneReplace, path); });
            if (!shipped) {
                Framework::Logging::GetLogger(kLogger)->warn("Resource '{}': its tables are not in the paks clients are sent (see the asset pak error above), so its item classes and recipes leave the catalog", resource);
                KCDC::Shared::Game::CustomRecipeRegistry::Get().RemoveTables(resource);
                KCDC::Shared::Game::ModItemRegistry::Get().Remove(resource);
                registered->second.clear();
            }
        }
        _unverified.clear();
    }
} // namespace KCDC::Features::StreamedAssets
