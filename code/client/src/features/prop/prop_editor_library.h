#pragma once

#include "prop_editor_favorites.h"
#include "sdk/types.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::Prop {
    /** One row of a catalog the library browses. */
    struct AssetLibraryItem {
        std::string name;                     // what the list shows
        std::string path;                     // the key: searched, favourited
        std::vector<std::string> directories; // where it sits in the tree
        std::string detail;                   // a fixed right-hand column, until a size is measured
    };

    /** What a library browses, and how it names things. */
    struct AssetLibraryCatalog {
        std::vector<AssetLibraryItem> (*build)() = nullptr;
        const char *rootName                     = "";              // the tree's root
        const char *plural                       = "items";         // "meshes", "effects"
        const char *prefsName                    = "library.prefs"; // beside the maps
        const char *searchHint                   = "Search...";
        std::vector<std::string> devRoots; // first directories hidden unless asked for

        /** The mesh catalog, as a tree from its full `objects/` path. */
        static AssetLibraryCatalog Props();

        /** The particle effect catalog, as `library.group.effect`. */
        static AssetLibraryCatalog Effects();
    };

    /**
     * A catalog as a browsable tree with multi-word search, project favorites
     * and global recents. Development folders are hidden unless
     * asked for. Indices are the catalog's own rows. Render thread only.
     */
    class AssetLibrary final {
      public:
        /** Nothing chosen. */
        static constexpr int kNone = -1;

        explicit AssetLibrary(AssetLibraryCatalog catalog): _catalog(std::move(catalog)) {}

        void Render(float height);

        /** The row in hand, or `kNone`. */
        int Selected() const {
            return _selected;
        }

        /** The row under the mouse this frame, or `kNone`. */
        int Hovered() const {
            return _hovered;
        }

        /** True once after the user clicks a row. */
        bool TakePicked(int &index);

        /** Records a row as just placed, for the recent list. */
        void NoteUsed(int index);

        /** Records a mesh's size, measured off its preview. */
        void NoteSize(int index, const SDK::Vec3 &size);

        /** Queries initialize the catalog without drawing or changing the current UI selection. Paths are canonical catalog keys. */
        const std::vector<AssetLibraryItem> &Items();
        const AssetLibraryItem *Find(const std::string &path);
        std::vector<int> Search(const std::string &query, bool includeDevelopment = false);
        std::vector<std::string> Favorites();
        std::vector<std::string> Recent();
        bool SetFavorite(const std::string &path, bool favorite);
        bool NoteUsed(const std::string &path);

        /** Project favorites are separate from the legacy global Favorites()/SetFavorite() API. */
        const FavoriteFolders &GetFavoriteFolders() const {
            return _favoriteFolders;
        }
        void SetFavoriteFolders(const FavoriteFolders &folders);
        void RestoreFavoriteFolders(const FavoriteFolders &folders);
        std::uint64_t FavoritesRevision() const {
            return _favoritesRevision;
        }
        std::uint32_t AddFavoriteFolder(const std::string &name, std::string &error);
        bool RenameFavoriteFolder(std::uint32_t id, const std::string &name, std::string &error);
        bool DeleteFavoriteFolder(std::uint32_t id, std::string &error);
        bool SetActiveFavoriteFolder(std::uint32_t id, std::string &error);
        bool SetProjectFavorite(const std::string &path, bool favorite, std::uint32_t folderId, std::string &error);
        bool MoveProjectFavorite(const std::string &path, std::uint32_t sourceId, std::uint32_t targetId, std::string &error);

      private:
        /** One directory of the catalog. Node 0 is the root. */
        struct Category {
            std::string name;
            std::vector<int> children;

            /** Every row at or below this directory. */
            std::vector<int> items;

            bool dev = false; // under a development-only root
        };

        /** The lists above the tree, or a tree node. */
        enum class Scope : std::uint8_t {
            Everything,
            Favorites,
            Recent,
            Category,
        };

        void EnsureBuilt();
        int ChildNamed(int parent, const std::string &name);
        bool IsDevRoot(const std::string &segment) const;
        bool IsValid(int index) const;
        int IndexOfPath(const std::string &path) const;

        void RenderTree(int node);
        void RenderScopes();
        void RenderFavoriteFolders();
        void FavoriteFoldersChanged();
        void RenderList(float height);
        void RefreshMatches();

        bool Passes(int index, const std::vector<std::string> &tokens) const;

        bool SizeOf(int index, SDK::Vec3 &size) const;
        bool IsFavorite(int index) const;
        void ToggleFavorite(int index);

        void LoadPrefs();
        void SavePrefs() const;

        AssetLibraryCatalog _catalog;

        bool _built = false;
        std::vector<AssetLibraryItem> _items;

        // A door's, smart object's or item's folders, which its key does not carry, for search to match `interactive/` or `items/Food`.
        std::vector<std::string> _keyFolders;
        std::unordered_map<std::string, int> _byPath;
        std::vector<Category> _categories;

        Scope _scope     = Scope::Everything;
        int _category    = 0;
        bool _showDev    = false;
        char _search[96] = {};

        int _selected = kNone;
        int _hovered  = kNone;
        bool _picked  = false;

        std::unordered_set<std::string> _favorites; // by path, so a regenerated catalog keeps them
        FavoriteFolders _favoriteFolders;
        std::uint64_t _favoritesRevision = 0;
        char _folderName[81]             = {};
        std::uint32_t _editingFolder     = 0;
        std::string _folderError;
        std::deque<std::string> _recent; // newest first
        std::unordered_map<int, SDK::Vec3> _sizes;

        // Rows passing the scope and search, recomputed only when either changes.
        std::vector<int> _matches;
        std::string _matchedSearch;
        Scope _matchedScope            = Scope::Everything;
        int _matchedCategory           = -1;
        bool _matchedDev               = false;
        std::uint64_t _revision        = 1; // bumped by favourites and recents
        std::uint64_t _matchedRevision = 0;
    };
} // namespace KCDC::Features::Prop
