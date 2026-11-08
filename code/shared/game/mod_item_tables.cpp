#include "mod_item_tables.h"

#include "clothing_armor_types.h"
#include "shared/xml/mini_xml.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <mutex>

namespace KCDC::Shared::Game {
    namespace {
        constexpr std::array<std::string_view, 4> kArmor       = {"Armor", "Helmet", "Hood", "QuickSlotContainer"};
        constexpr std::array<std::string_view, 2> kWeapons     = {"MeleeWeapon", "MissileWeapon"};
        constexpr std::array<std::string_view, 4> kConsumables = {"Food", "Herb", "Poison", "Ointment"};
        constexpr std::array<std::string_view, 13> kImmutable  = {"AlchemyBase", "Ammo", "CraftingMaterial", "DiceBadge", "Die", "Document", "Key", "KeyRing", "Misc", "MiscItem", "Money", "NPCTool", "PickableItem"};
        constexpr std::array<std::string_view, 10> kDivisible  = {"CraftingMaterial", "Die", "Food", "Herb", "KeyRing", "Misc", "MiscItem", "Money", "Ointment", "Poison"};

        template <std::size_t N>
        bool In(const std::array<std::string_view, N> &set, std::string_view value) {
            return std::find(set.begin(), set.end(), value) != set.end();
        }

        bool ReadQuality(std::string_view text, std::uint32_t fallback, std::uint32_t &out) {
            if (text.empty()) {
                out = fallback;
                return true;
            }
            std::uint32_t value = 0;
            const auto result   = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc {} || result.ptr != text.data() + text.size() || value < 1 || value > 4) {
                return false;
            }
            out = value;
            return true;
        }

        bool ReadFlag(std::string_view text, bool &out) {
            if (text.empty() || text == "false") {
                out = false;
                return true;
            }
            if (text == "true") {
                out = true;
                return true;
            }
            return false;
        }

        std::string Lower(std::string_view text) {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        /** What an alias copies from the class it names: a shipped `InventoryPropertyClass` or another mod's `ModItemClass`. */
        template <typename Source>
        void CopyConstraints(const Source &source, ModItemClass &out) {
            out.maximumQuality  = source.maximumQuality;
            out.canHealthChange = source.canHealthChange;
            out.equippable      = source.equippable;
            out.breakableWeapon = source.breakableWeapon;
            out.nativeStackable = source.nativeStackable;
        }
    } // namespace

    namespace {
        void CollectClothingTypes(const Xml::Element &node, std::string_view inherited, std::string_view parent, ModClothingTypes &out) {
            const std::string_view armorType = node.HasAttribute("ArmorType") ? node.Attribute("ArmorType") : inherited;
            std::string_view enclosing       = parent;
            if (node.tag == "Clothing" && node.HasAttribute("Name")) {
                out[std::string(node.Attribute("Name"))] = ModClothingComponent {std::string(armorType), std::string(parent)};
                enclosing                                = node.Attribute("Name");
            }
            for (const Xml::Element &child : node.children) {
                CollectClothingTypes(child, armorType, enclosing, out);
            }
        }
    } // namespace

    bool ParseModClothingTypes(std::string_view xml, ModClothingTypes &out, std::string &error) {
        Xml::Element root;
        if (!Xml::Parse(xml, root, error)) {
            return false;
        }
        CollectClothingTypes(root, {}, {}, out);
        return true;
    }

    bool IsImmutableModClothing(const ModClothingTypes &clothing, std::string_view component) {
        // Up the patch's own tree while it names no type, then the game's table for the first
        // component it knows. Bounded: a patch cannot nest deeper than this.
        for (int depth = 0; depth < 32 && !component.empty(); ++depth) {
            const auto own = clothing.find(component);
            if (own == clothing.end()) {
                return IsImmutableClothingComponent(component);
            }
            if (!own->second.armorType.empty()) {
                return IsImmutableArmorType(own->second.armorType);
            }
            if (IsImmutableClothingComponent(component)) {
                return true;
            }
            component = own->second.parent;
        }
        return false;
    }

    bool DeriveModItemClass(std::string_view category, const ItemAttributeReader &attribute, const ModClothingTypes &clothing, ModItemClass &out, std::string &reason) {
        out          = ModItemClass {};
        out.name     = std::string(attribute("Name"));
        out.guid     = Lower(attribute("Id"));
        out.category = std::string(category);
        out.nameKey  = std::string(attribute("UIName"));
        if (out.name.empty() || out.guid.empty()) {
            reason = "no Name or Id";
            return false;
        }
        if (ItemClassIdFromGuid(out.guid) == RPC::ItemClassId {}) {
            reason = "Id is not a GUID";
            return false;
        }
        const std::string_view price = attribute("Price");
        if (!price.empty()) {
            const auto result = std::from_chars(price.data(), price.data() + price.size(), out.price);
            if (result.ec != std::errc {} || result.ptr != price.data() + price.size() || out.price > 0x7FFFFFFFU) {
                reason = "Price is not a whole number";
                return false;
            }
        }
        if (!ReadFlag(attribute("IsQuestItem"), out.questItem)) {
            reason = "IsQuestItem is neither true nor false";
            return false;
        }
        bool divisible = false;
        if (!ReadFlag(attribute("IsDivisible"), divisible)) {
            reason = "IsDivisible is neither true nor false";
            return false;
        }

        if (category == "ItemAlias") {
            for (const char *overridden : {"MaxQuality", "Clothing", "IsBreakable", "CanHealthChange", "IsDivisible"}) {
                if (!attribute(overridden).empty()) {
                    reason = std::string("an ItemAlias overriding ") + overridden + " is not supported";
                    return false;
                }
            }
            out.aliasOf = Lower(attribute("SourceItemId"));
            if (ItemClassIdFromGuid(out.aliasOf) == RPC::ItemClassId {}) {
                reason = "SourceItemId is not a GUID";
                return false;
            }
            return true;
        }
        if (In(kArmor, category)) {
            const std::string_view component = attribute("Clothing");
            const bool immutable             = !component.empty() && IsImmutableModClothing(clothing, component);
            if (!ReadQuality(attribute("MaxQuality"), 3, out.maximumQuality)) {
                reason = "MaxQuality is not 1 to 4";
                return false;
            }
            if (immutable) {
                out.maximumQuality = 1;
            }
            out.canHealthChange = !immutable;
            out.equippable      = true;
        }
        else if (In(kWeapons, category)) {
            if (!ReadQuality(attribute("MaxQuality"), 4, out.maximumQuality)) {
                reason = "MaxQuality is not 1 to 4";
                return false;
            }
            out.canHealthChange = true;
            out.equippable      = true;
            if (!ReadFlag(attribute("IsBreakable"), out.breakableWeapon)) {
                reason = "IsBreakable is neither true nor false";
                return false;
            }
        }
        else if (In(kConsumables, category)) {
            out.canHealthChange = true;
        }
        else if (In(kImmutable, category)) {
            out.equippable = category == "Ammo";
        }
        else {
            reason = "category " + std::string(category) + " is not one the catalog knows";
            return false;
        }
        out.nativeStackable = category == "Ammo" || (In(kDivisible, category) && divisible);
        return true;
    }

    bool ParseModItemTable(std::string_view xml, const ModClothingTypes &clothing, std::vector<ModItemClass> &out, std::string &error) {
        Xml::Element root;
        if (!Xml::Parse(xml, root, error)) {
            return false;
        }
        const Xml::Element *classes = root.tag == "ItemClasses" ? &root : root.Child("ItemClasses");
        if (classes == nullptr) {
            error = "no <ItemClasses> element";
            return false;
        }
        for (const Xml::Element &child : classes->children) {
            ModItemClass item;
            std::string reason;
            const ItemAttributeReader attribute = [&child](const char *name) { return child.Attribute(name); };
            if (DeriveModItemClass(child.tag, attribute, clothing, item, reason)) {
                out.push_back(std::move(item));
            }
        }
        return true;
    }

    ModItemRegistry &ModItemRegistry::Get() {
        static ModItemRegistry registry;
        return registry;
    }

    void ModItemRegistry::Replace(std::string_view source, std::vector<ModItemClass> items) {
        std::unique_lock lock(_mutex);
        if (items.empty()) {
            if (const auto owned = _sources.find(source); owned != _sources.end()) {
                _sources.erase(owned);
            }
        }
        else {
            _sources.insert_or_assign(std::string(source), std::move(items));
        }
        Rebuild();
        ++_revision;
    }

    void ModItemRegistry::Remove(std::string_view source) {
        Replace(source, {});
    }

    void ModItemRegistry::Reset() {
        std::unique_lock lock(_mutex);
        _entries.clear();
        _sources.clear();
        ++_revision;
    }

    void ModItemRegistry::Rebuild() {
        // The first source in name order to define a GUID keeps it; a GUID the game ships is the
        // compiled table's. Only the shipped lookups are consulted: anything else would take this
        // registry's lock again.
        std::map<std::string, std::pair<const std::string *, const ModItemClass *>, std::less<>> winners;
        for (const auto &[source, items] : _sources) {
            for (const ModItemClass &item : items) {
                if (FindShippedItemClass(item.guid) == nullptr) {
                    winners.try_emplace(item.guid, &source, &item);
                }
            }
        }

        // Aliases copy the class they name once it is settled: a plain class, a shipped one, or an
        // alias settled in an earlier pass. One that never settles names nothing anybody has.
        std::map<std::string, std::pair<const std::string *, ModItemClass>, std::less<>> settled;
        std::vector<std::pair<const std::string *, const ModItemClass *>> aliases;
        for (const auto &[guid, winner] : winners) {
            if (winner.second->aliasOf.empty()) {
                settled.emplace(guid, std::make_pair(winner.first, *winner.second));
            }
            else {
                aliases.push_back(winner);
            }
        }
        for (bool progress = true; progress && !aliases.empty();) {
            progress = false;
            for (auto alias = aliases.begin(); alias != aliases.end();) {
                ModItemClass resolved = *alias->second;
                if (const auto target = settled.find(resolved.aliasOf); target != settled.end()) {
                    CopyConstraints(target->second.second, resolved);
                }
                else if (const InventoryPropertyClass *shipped = FindShippedInventoryPropertyClass(resolved.aliasOf)) {
                    CopyConstraints(*shipped, resolved);
                }
                else {
                    ++alias;
                    continue;
                }
                // The key is copied first: the pair below takes `resolved` apart.
                std::string guid = resolved.guid;
                settled.emplace(std::move(guid), std::make_pair(alias->first, std::move(resolved)));
                alias    = aliases.erase(alias);
                progress = true;
            }
        }

        // A GUID keeps one entry for the life of the process: callers hold on to a class they
        // resolved -- vendor stock does -- so a class whose definition changes is rewritten in
        // place, and one that leaves the catalog is only marked so. Lookups answer live ones.
        for (auto &[guid, entry] : _entries) {
            if (settled.find(guid) == settled.end()) {
                entry.live = false;
            }
        }
        for (auto &[guid, wanted] : settled) {
            Entry &entry = _entries[guid];
            entry.live   = true;
            if (entry.source == *wanted.first && entry.data == wanted.second) {
                continue;
            }
            entry.data       = std::move(wanted.second);
            entry.source     = *wanted.first;
            entry.item       = ItemClassEntry {entry.data.name, entry.data.guid, entry.data.category, entry.data.price, entry.data.nameKey};
            entry.properties = InventoryPropertyClass {entry.data.guid, entry.data.maximumQuality, entry.data.canHealthChange, entry.data.equippable, entry.data.breakableWeapon, entry.data.questItem, entry.data.nativeStackable};
        }
    }

    const ItemClassEntry *ModItemRegistry::Find(std::string_view guid) const {
        std::shared_lock lock(_mutex);
        const auto found = _entries.find(guid);
        return found == _entries.end() || !found->second.live ? nullptr : &found->second.item;
    }

    const InventoryPropertyClass *ModItemRegistry::FindProperties(std::string_view guid) const {
        std::shared_lock lock(_mutex);
        const auto found = _entries.find(guid);
        return found == _entries.end() || !found->second.live ? nullptr : &found->second.properties;
    }

    const ItemClassEntry *ModItemRegistry::FindByName(std::string_view name) const {
        std::shared_lock lock(_mutex);
        for (const auto &[guid, entry] : _entries) {
            if (entry.live && entry.data.name == name) {
                return &entry.item;
            }
        }
        return nullptr;
    }

    void ModItemRegistry::ForEach(const fu2::function<void(const ItemClassEntry &) const> &visit) const {
        // Visited outside the lock: a visitor asks the catalog about each class it is handed, and a
        // shared lock taken twice on one thread is not one a waiting writer lets through.
        std::vector<ItemClassEntry> items;
        {
            std::shared_lock lock(_mutex);
            items.reserve(_entries.size());
            for (const auto &[guid, entry] : _entries) {
                if (entry.live) {
                    items.push_back(entry.item);
                }
            }
        }
        for (const ItemClassEntry &item : items) {
            visit(item);
        }
    }

    std::size_t ModItemRegistry::Size() const {
        std::shared_lock lock(_mutex);
        return static_cast<std::size_t>(std::count_if(_entries.begin(), _entries.end(), [](const auto &entry) { return entry.second.live; }));
    }

    std::uint64_t ModItemRegistry::Revision() const {
        std::shared_lock lock(_mutex);
        return _revision;
    }
} // namespace KCDC::Shared::Game
