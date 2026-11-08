#pragma once

#include "inventory_property_classes.h"
#include "item_classes.h"

#include <cstddef>
#include <cstdint>
#include <function2/function2.hpp>
#include <map>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /**
     * One item class a streamed KCD2 mod adds through a table patch
     * (`libs/tables/item/item__<id>.xml`): what the compiled catalog holds for
     * a shipped class, derived at runtime by the rules the two catalog
     * generators apply offline.
     */
    struct ModItemClass {
        std::string name;
        std::string guid;
        std::string category;
        std::string nameKey;
        // An `ItemAlias`'s `SourceItemId`, lower case: the class whose constraints it copies. The
        // registry resolves it against every source it holds and the shipped table, so an alias may
        // name a class later in its file or in another resource's.
        std::string aliasOf;
        std::uint32_t price          = 0;
        std::uint32_t maximumQuality = 1;
        bool canHealthChange         = false;
        bool equippable              = false;
        bool breakableWeapon         = false;
        bool questItem               = false;
        bool nativeStackable         = false;

        bool operator==(const ModItemClass &) const = default;
    };

    /** Reads one attribute of an `<ItemClasses>` child; empty for one the element does not carry. */
    using ItemAttributeReader = fu2::function<std::string_view(const char *name) const>;

    /** The largest table patch the catalog reads; the game's own item table is a few megabytes. */
    inline constexpr std::size_t kMaximumModTableBytes = 32 * 1024 * 1024;

    /**
     * One clothing component a mod's `CharacterComponent__<id>.xml` defines: the
     * armor type written on it or an ancestor in the patch, if any, and the
     * nearest enclosing `<Clothing>` name. A patch usually repeats only the
     * names of the game's tree down to its own leaves, so the type is often
     * found through the parent in the game's compiled table.
     */
    struct ModClothingComponent {
        std::string armorType;
        std::string parent;
    };
    using ModClothingTypes = std::map<std::string, ModClothingComponent, std::less<>>;

    /** Reads a CharacterComponent patch: every `<Clothing Name>` with what the tree says of it. */
    bool ParseModClothingTypes(std::string_view xml, ModClothingTypes &out, std::string &error);

    /** Whether a component's items never change condition: its own type, else its parents', else the game's table. */
    bool IsImmutableModClothing(const ModClothingTypes &clothing, std::string_view component);

    /**
     * The catalog row for one `<ItemClasses>` child, by the rules of
     * `tools/generate_item_catalog.py` and `tools/generate_inventory_properties.py`:
     * the element name is the category; armor takes `MaxQuality` (3 unless
     * said), weapons too (4) with `IsBreakable`; consumables and the fixed
     * kinds are quality 1; `IsQuestItem` and `IsDivisible` as authored; an
     * `ItemAlias` records its `SourceItemId` in `aliasOf`, and copies that
     * class's constraints once the registry resolves it.
     *
     * Armor whose `Clothing` component carries an immutable armor type (spurs,
     * belts, pouches, horse tack...) is quality 1 and never changes condition,
     * as the shipped table has it: the component is looked up in `clothing`,
     * the mod's own CharacterComponent patch, and failing that in the game's
     * compiled component table.
     *
     * @return False, with `reason`, for a child that is no item class or one
     *         the rules do not cover.
     */
    bool DeriveModItemClass(std::string_view category, const ItemAttributeReader &attribute, const ModClothingTypes &clothing, ModItemClass &out, std::string &reason);

    /** Every item class of one table patch, from its XML text. False only for XML the reader cannot follow or a file with no `<ItemClasses>`. */
    bool ParseModItemTable(std::string_view xml, const ModClothingTypes &clothing, std::vector<ModItemClass> &out, std::string &error);

    /**
     * The item classes streamed mods add this session, by the source that
     * brought them: a resource on the server, `<resource> <pak>` on the client.
     * The catalog lookups consult it after the custom items and the compiled
     * table, so a mod's class is one `Inventory.add`, a projection and every
     * other catalog user accept.
     *
     * What the catalog answers is rebuilt from every source on each change,
     * the same way on both ends: sources in name order, so the resource whose
     * name sorts first keeps a GUID two of them define; a GUID the game ships
     * keeps its compiled entry; an alias copies whichever class it names once
     * that is known, from any source.
     *
     * A GUID keeps one entry for the life of the process, so a class pointer a
     * caller holds -- vendor stock holds them -- never dangles: a changed
     * definition is rewritten in place, and a class whose source left stays
     * readable but is no longer found. `Reset` is the exception, for tests.
     */
    class ModItemRegistry final {
      public:
        static ModItemRegistry &Get();

        /** Replaces what `source` contributes. */
        void Replace(std::string_view source, std::vector<ModItemClass> items);
        void Remove(std::string_view source);
        void Reset();

        const ItemClassEntry *Find(std::string_view guid) const;
        const InventoryPropertyClass *FindProperties(std::string_view guid) const;
        const ItemClassEntry *FindByName(std::string_view name) const;
        void ForEach(const fu2::function<void(const ItemClassEntry &) const> &visit) const;
        std::size_t Size() const;
        /** Advances on every change; a cache keyed on the catalog can tell staleness by it. */
        std::uint64_t Revision() const;

      private:
        struct Entry {
            ModItemClass data;
            std::string source;
            ItemClassEntry item;
            InventoryPropertyClass properties;
            // In the catalog now; an entry whose source left stays for those still holding it.
            bool live = false;
        };

        /** Brings `_entries` in line with `_sources`; the caller holds the lock exclusively. */
        void Rebuild();

        mutable std::shared_mutex _mutex;
        // A map keeps each entry's address while others come and go, and none is erased but by
        // Reset; the views in `item` and `properties` point into the entry's own strings.
        std::map<std::string, Entry, std::less<>> _entries;
        // What each source brought, as read: aliases still unresolved.
        std::map<std::string, std::vector<ModItemClass>, std::less<>> _sources;
        std::uint64_t _revision = 0;
    };
} // namespace KCDC::Shared::Game
