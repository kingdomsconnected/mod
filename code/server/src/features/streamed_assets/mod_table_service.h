#pragma once

#include <integrations/server/asset_paks/asset_pak_builder.h>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Framework::Scripting {
    class ResourceManager;
}

namespace KCDC::Features::StreamedAssets {
    /**
     * Keeps the item catalog and the crafting recipes in step with the KCD2
     * mods the resources stream.
     *
     * A resource that ships `replace/libs/tables/item/item__<id>.xml` adds item
     * classes the compiled catalog never saw, and one that ships
     * `replace/libs/tables/minigame/alchemyrecipe__<id>.xml` or
     * `blacksmithrecipes__<id>.xml` adds recipes, or replaces the game's of the
     * same id. The server reads those files once the resource has started --
     * after its own scripts and `resourceStart` handlers, which the resource
     * manager runs first -- walking the lane the way the asset pak builder
     * packs it, and registers their classes with `ModItemRegistry` and then
     * their recipes with `CustomRecipeRegistry`; when the resource stops they
     * go. A resource whose scripts name a mod's classes as they start lists
     * the mod's resource as a dependency, which starts first. Clients read the
     * same tables out of the pak they mount, so both ends name the same
     * classes and brew and forge the same recipes.
     *
     * The walk cannot know every reason the builder might still refuse the
     * lane -- an asset past its size limit, say -- so on the next tick, once the
     * pak is built, a resource whose tables did not make it into the shipped
     * paks takes its classes out again. Two limits remain, both for a lane the
     * builder has already reported as not shipped: the builder answers by path
     * across every resource, so another resource shipping the same table path
     * hides the failure; and a dependant's start handlers, which run before
     * that tick, may already have named the classes.
     */
    class ModTableService final {
      public:
        /** Follows the resource manager's start and stop for the server's life. */
        void Install(Framework::Scripting::ResourceManager &resources);

        /** Reads a resource's item and recipe table patches under `root` and registers them under `resource`; the item classes read. */
        std::size_t Register(const std::string &resource, const std::filesystem::path &root);
        void Unregister(const std::string &resource);

        /** Main thread, every tick: drops the classes and recipes of a resource whose tables the built paks do not carry. */
        void VerifyShipped(const Framework::Integrations::Server::AssetPakBuilder &paks);

      private:
        bool _installed = false;
        // The replace-lane paths of each registered resource's item and recipe tables, and the resources whose
        // paths are still to be checked against the built paks.
        std::map<std::string, std::vector<std::string>> _tables;
        std::set<std::string> _unverified;
    };
} // namespace KCDC::Features::StreamedAssets
