#include "native_inventory_projection.h"
#include "features/items/custom_item_module.h"
#include "native_item_effects.h"

#include "sdk/wh/entitymodule/c_missile_weapon_runtime_data.h"
#include "shared/game/item_classes.h"
#include "shared/game/missile_catalog.h"
#include "shared/inventory_item_properties.h"
#include "shared/inventory_property_policy.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace KCDC::Features::Items {
    namespace {
        // Native condition conversion uses single-precision interval math.
        constexpr float kPropertyRoundingTolerance = 0.0001F;

        struct Group {
            KCDC::Shared::RPC::InventoryItem properties;
            std::uint32_t amount   = 0;
            std::uint32_t retained = 0;
            std::vector<NativeUnit> units;
        };

        bool SameClass(const SDK::wh::entitymodule::C_Item &native, const KCDC::Shared::RPC::InventoryItem &properties) {
            const auto itemClass = native.InvokeGetItemClassRaw();
            const auto expected  = KCDC::Features::Items::CustomItemModule::Get().ToNative(properties.itemClass);
            return itemClass.m_low == expected.m_low && itemClass.m_high == expected.m_high;
        }

        void ReadProperties(SDK::wh::entitymodule::C_Item &native, KCDC::Shared::RPC::InventoryItem &properties) {
            KCDC::Features::Items::CustomItemModule::Get().Read(native, properties);
            properties.quality       = native.InvokeGetQualityRaw();
            properties.health        = native.GetHealth();
            properties.condition     = native.InvokeGetConditionRaw();
            properties.poisonBuff    = {};
            properties.poisonCharges = 0;
            std::string error;
            if (!ReadNativeItemEffects(native, properties.onEquipBuffs, error)) {
                throw std::runtime_error(error);
            }
            if (KCDC::Shared::Game::FindMissileClass(properties.itemClass) != nullptr) {
                auto *runtime = static_cast<SDK::wh::entitymodule::C_WeaponEquipRuntimeData *>(native.GetRuntimeData());
                if (runtime != nullptr) {
                    properties.poisonBuff    = {runtime->GetPoisonBuff().hipart, runtime->GetPoisonBuff().lopart};
                    properties.poisonCharges = runtime->GetPoisonCharges();
                }
            }
        }

        /** Whether `native` carries `properties`; `effects` false leaves its item effects out of it. */
        bool Matches(SDK::wh::entitymodule::C_Item &native, const KCDC::Shared::RPC::InventoryItem &properties, bool effects = true) {
            if (!SameClass(native, properties) || !KCDC::Features::Items::CustomItemModule::Get().Matches(native, properties) || native.InvokeGetQualityRaw() != properties.quality) {
                return false;
            }
            const auto health    = native.GetHealth();
            const auto condition = native.InvokeGetConditionRaw();
            if (!std::isfinite(health) || !std::isfinite(condition) || std::abs(health - properties.health) > kPropertyRoundingTolerance || std::abs(condition - properties.condition) > kPropertyRoundingTolerance) {
                return false;
            }
            std::string error;
            std::vector<KCDC::Shared::RPC::ItemClassId> held;
            if (effects && (!ReadNativeItemEffects(native, held, error) || held != properties.onEquipBuffs)) {
                return false;
            }
            if (KCDC::Shared::Game::FindMissileClass(properties.itemClass) != nullptr) {
                auto *runtime = static_cast<SDK::wh::entitymodule::C_WeaponEquipRuntimeData *>(native.GetRuntimeData());
                if (runtime == nullptr) {
                    return !properties.poisonBuff.IsValid() && properties.poisonCharges == 0;
                }
                return runtime->GetPoisonBuff().hipart == properties.poisonBuff.low && runtime->GetPoisonBuff().lopart == properties.poisonBuff.high && runtime->GetPoisonCharges() == properties.poisonCharges;
            }
            return true;
        }

        void WriteProperties(SDK::wh::entitymodule::C_Item &native, const KCDC::Shared::RPC::InventoryItem &properties) {
            KCDC::Features::Items::CustomItemModule::Get().Bind(native, properties);
            if (native.InvokeGetQualityRaw() != properties.quality) {
                native.SetQuality(properties.quality);
            }
            if (native.GetHealth() != properties.health) {
                native.InvokeSetItemHealthRaw(properties.health);
            }
            std::string error;
            if (!ApplyNativeItemEffects(native, properties, error)) {
                throw std::runtime_error(error);
            }
            if (KCDC::Shared::Game::FindMissileClass(properties.itemClass) != nullptr) {
                auto *runtime = static_cast<SDK::wh::entitymodule::C_WeaponEquipRuntimeData *>(native.GetRuntimeData());
                if (runtime == nullptr && properties.poisonBuff.IsValid()) {
                    runtime = static_cast<SDK::wh::entitymodule::C_WeaponEquipRuntimeData *>(native.EnsureRuntimeData());
                }
                if (runtime != nullptr) {
                    runtime->SetPoison({properties.poisonBuff.low, properties.poisonBuff.high}, properties.poisonCharges);
                }
            }
            if (!Matches(native, properties)) {
                throw std::runtime_error("The game did not keep the properties it was given");
            }
        }

        /** A row as the projection needs it, or an exception naming why it cannot be shown. */
        KCDC::Shared::RPC::InventoryItem ParseRow(const nlohmann::json &row) {
            const auto &itemClass = row.at("item").get_ref<const std::string &>();
            if (KCDC::Shared::Game::FindItemClass(itemClass) == nullptr) {
                throw std::runtime_error("Unknown item class " + itemClass);
            }
            KCDC::Shared::RPC::InventoryItem properties;
            properties.inventoryItemId = 1;
            properties.itemClass       = KCDC::Shared::Game::ItemClassIdFromGuid(itemClass);
            properties.amount          = row.at("amount").get<std::uint32_t>();
            if (!KCDC::Shared::Inventory::ApplyItemProperties(row.at("metadata"), properties) || !KCDC::Shared::Inventory::AreSupportedOnEquipBuffs(properties)) {
                throw std::runtime_error("Invalid properties for " + itemClass);
            }
            if (properties.quality == 0 || properties.health < 0.0F || properties.condition < 0.0F) {
                throw std::runtime_error("Properties for " + itemClass + " are not concrete");
            }
            if ((properties.poisonBuff.IsValid() || properties.poisonCharges != 0) && KCDC::Shared::Game::FindMissileClass(properties.itemClass) == nullptr) {
                throw std::runtime_error("Poison on a class that cannot carry it: " + itemClass);
            }
            return properties;
        }

        std::string GroupKey(const std::string &itemClass, const KCDC::Shared::RPC::InventoryItem &properties) {
            return itemClass + '\n' + KCDC::Shared::Inventory::EncodeItemProperties(properties).dump();
        }

        /** The loss rule the server applies to wear: something went down and nothing went up. */
        bool IsWear(const KCDC::Shared::RPC::InventoryItem &before, const KCDC::Shared::RPC::InventoryItem &after) {
            const bool keepsPoison = after.poisonBuff == before.poisonBuff || (!after.poisonBuff.IsValid() && after.poisonCharges == 0);
            const bool noGain      = after.quality <= before.quality && (after.quality < before.quality || after.health <= before.health) && after.poisonCharges <= before.poisonCharges && after.onEquipBuffs == before.onEquipBuffs && keepsPoison;
            const bool lost        = after.quality < before.quality || after.health < before.health || after.poisonCharges < before.poisonCharges || after.poisonBuff != before.poisonBuff;
            return noGain && lost;
        }
    } // namespace

    bool NativeInventoryProjection::ReadNativeRow(SDK::wh::entitymodule::C_Item &item, nlohmann::json &row, std::string &error) {
        const auto nativeClass = item.InvokeGetItemClassRaw();
        KCDC::Shared::RPC::InventoryItem properties;
        properties.itemClass = {nativeClass.m_low, nativeClass.m_high};
        properties.amount    = item.GetAmount();
        try {
            ReadProperties(item, properties);
        }
        catch (const std::exception &failure) {
            error = failure.what();
            return false;
        }
        const auto text = KCDC::Shared::Game::CryGuidToText(properties.itemClass.low, properties.itemClass.high);
        const std::string itemClass(text.data(), text.size());
        nlohmann::json canonical;
        if (!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(itemClass, KCDC::Shared::Inventory::EncodeItemProperties(properties), canonical, error)) {
            return false;
        }
        row = {{"item", itemClass}, {"amount", item.GetAmount()}, {"metadata", std::move(canonical)}};
        return true;
    }

    bool NativeInventoryProjection::SameProperties(SDK::wh::entitymodule::C_Item &left, SDK::wh::entitymodule::C_Item &right) {
        const auto leftClass  = left.InvokeGetItemClassRaw();
        const auto rightClass = right.InvokeGetItemClassRaw();
        if (leftClass.m_low != rightClass.m_low || leftClass.m_high != rightClass.m_high) {
            return false;
        }
        KCDC::Shared::RPC::InventoryItem a, b;
        a.itemClass = b.itemClass = {leftClass.m_low, leftClass.m_high};
        try {
            ReadProperties(left, a);
            ReadProperties(right, b);
        }
        catch (const std::exception &) {
            return false;
        }
        return a.quality == b.quality && a.health == b.health && a.condition == b.condition && a.poisonBuff == b.poisonBuff && a.poisonCharges == b.poisonCharges && a.onEquipBuffs == b.onEquipBuffs && a.custom == b.custom && a.customStackKey == b.customStackKey;
    }

    bool NativeInventoryProjection::Apply(SDK::wh::entitymodule::C_Inventory *inventory, const std::map<std::string, nlohmann::json> &rows, std::map<std::string, std::string> &unshown, std::string &error,
        const fu2::function<void(SDK::wh::entitymodule::C_Item &) const> &takeOff) {
        unshown.clear();
        auto &customItems = KCDC::Features::Items::CustomItemModule::Get();
        if (!customItems.Ready()) {
            error = customItems.Failure().empty() ? "Waiting for item definitions" : customItems.Failure();
            return false;
        }
        for (const auto &[id, row] : rows) {
            if (KCDC::Shared::Game::FindItemClass(row.at("item").get<std::string>()) == nullptr) {
                error = "Waiting for the item definition";
                return false;
            }
        }
        if (inventory == nullptr) {
            error = "The native inventory is unavailable";
            return false;
        }
        try {
            if (_inventory != inventory) {
                _native.clear();
                _failedKinds.clear();
                _inventory   = inventory;
                _initialized = false;
            }

            // Rows of one class with equal properties are one kind of native
            // stack. A row the game cannot show is set aside on its own, so
            // it never keeps the rest of the inventory from appearing.
            std::map<std::string, Group> groups;
            std::map<std::string, std::string> rowGroups;
            for (const auto &[id, row] : rows) {
                try {
                    const auto properties = ParseRow(row);
                    const auto key        = GroupKey(row.at("item").get<std::string>(), properties);
                    auto &group           = groups[key];
                    if (properties.amount > KCDC::Shared::RPC::kMaxInventoryStackAmount - group.amount) {
                        throw std::runtime_error("One kind of item exceeds what the game can count");
                    }
                    group.properties = properties;
                    group.amount += properties.amount;
                    group.units.push_back({id, properties.amount});
                    rowGroups.emplace(id, key);
                }
                catch (const std::exception &failure) {
                    unshown.insert_or_assign(id, failure.what());
                }
            }

            // Keep each native stack that holds rows the server still has, or
            // that matches a kind the server has room for; remove the rest.
            // Worn stacks are kept first: when a kind shrinks, its loose
            // copies go and the body keeps what it wears.
            std::map<std::uint64_t, NativeState> next;
            std::vector<std::pair<std::uint64_t, std::uint32_t>> removals;
            std::vector<SDK::wh::entitymodule::C_Item *> owned;
            for (std::size_t index = 0; index < inventory->GetOwnedItemCount(); ++index) {
                owned.push_back(inventory->GetOwnedItemAt(index));
            }
            std::stable_partition(owned.begin(), owned.end(), [](const SDK::wh::entitymodule::C_Item *item) {
                return item != nullptr && item->IsEquipped();
            });
            for (auto *item : owned) {
                if (item == nullptr || item->GetOwner() != inventory || item->GetAmount() == 0 || _lent.contains(item->GetId())) {
                    continue;
                }
                const auto known = _native.find(item->GetId());
                std::string key;
                if (known != _native.end()) {
                    for (const auto &unit : known->second.units) {
                        const auto found = rowGroups.find(unit.id);
                        if (found == rowGroups.end() || (!key.empty() && key != found->second)) {
                            key.clear();
                            break;
                        }
                        key = found->second;
                    }
                }
                // A repair moves one unit to a row of its own and leaves the
                // untouched copies where they were, so a stack follows the
                // kind its health now is rather than the row it held -- unless
                // it carries wear the rows have not caught up with yet. Item
                // effects are left out: a repair perk's are corrected below.
                if (!key.empty() && !known->second.wearPending && !Matches(*item, groups.at(key).properties, false)) {
                    const auto moved = std::find_if(groups.begin(), groups.end(), [&item](const auto &entry) {
                        return entry.second.retained < entry.second.amount && Matches(*item, entry.second.properties, false);
                    });
                    if (moved != groups.end()) {
                        key = moved->first;
                    }
                }
                if (key.empty() || groups.at(key).retained >= groups.at(key).amount) {
                    key.clear();
                    for (const auto &[candidate, group] : groups) {
                        if (group.retained < group.amount && Matches(*item, group.properties)) {
                            key = candidate;
                            break;
                        }
                    }
                }
                if (key.empty()) {
                    removals.push_back({item->GetId(), item->GetAmount()});
                    continue;
                }
                auto &group = groups.at(key);
                NativeState state {key, group.properties, {}, false};
                if (!Matches(*item, group.properties)) {
                    if (known != _native.end() && known->second.wearPending && Matches(*item, known->second.properties)) {
                        state.properties  = known->second.properties;
                        state.wearPending = true;
                    }
                    else {
                        // A stack that will not take the row's properties is
                        // replaced by a fresh one rather than kept wrong.
                        try {
                            WriteProperties(*item, group.properties);
                        }
                        catch (const std::exception &) {
                            removals.push_back({item->GetId(), item->GetAmount()});
                            continue;
                        }
                    }
                }
                const auto keep = std::min(item->GetAmount(), group.amount - group.retained);
                group.retained += keep;
                if (keep < item->GetAmount()) {
                    removals.push_back({item->GetId(), item->GetAmount() - keep});
                }
                next.insert_or_assign(item->GetId(), std::move(state));
            }
            for (const auto &[wuid, amount] : removals) {
                auto *item = inventory->FindOwnedItemById(wuid);
                // A worn stack that goes whole comes off the body first.
                if (item != nullptr && takeOff && item->IsEquipped() && amount == item->GetAmount()) {
                    takeOff(*item);
                    item = inventory->FindOwnedItemById(wuid);
                }
                if (item != nullptr) {
                    inventory->RemoveItem(item, SDK::wh::entitymodule::C_Inventory::kScriptRemovalReason, amount);
                }
            }
            std::erase_if(next, [inventory](const auto &entry) {
                return inventory->FindOwnedItemById(entry.first) == nullptr;
            });
            _native = std::move(next);

            // Create what is missing. CreateItems can report failure after
            // adding the stack, so count the change rather than trust the
            // return, and never call it twice for one kind. A kind the game
            // will not create as asked is taken back out whole, and its rows
            // are set aside like a row that did not parse.
            std::erase_if(_failedKinds, [&groups](const auto &entry) {
                return !groups.contains(entry.first);
            });
            std::map<std::string, std::string> failedGroups;
            for (auto &[key, group] : groups) {
                const std::uint32_t missing = group.amount - group.retained;
                if (missing == 0) {
                    continue;
                }
                // C_RPGItemHealth::ResolveQualityAndHealth makes creation a
                // pure function of class, quality and condition, so a kind
                // that failed is not created and destroyed again on every
                // pass -- only once the server's rows of it change.
                if (const auto failed = _failedKinds.find(key); failed != _failedKinds.end() && failed->second.units == group.units) {
                    failedGroups.emplace(key, failed->second.reason);
                    continue;
                }
                std::map<std::uint64_t, std::uint32_t> before;
                for (std::size_t index = 0; index < inventory->GetOwnedItemCount(); ++index) {
                    if (auto *item = inventory->GetOwnedItemAt(index)) {
                        before.emplace(item->GetId(), item->GetAmount());
                    }
                }
                const auto &properties = group.properties;
                inventory->CreateItems(customItems.ToNative(properties.itemClass), static_cast<int>(missing), false, properties.quality, properties.health, properties.condition);
                // Every stack of the class that grew, by how much.
                std::vector<std::pair<std::uint64_t, std::uint32_t>> grown;
                for (std::size_t index = 0; index < inventory->GetOwnedItemCount(); ++index) {
                    auto *item = inventory->GetOwnedItemAt(index);
                    if (item == nullptr || item->GetOwner() != inventory || !SameClass(*item, properties)) {
                        continue;
                    }
                    const auto old   = before.find(item->GetId());
                    const auto count = old == before.end() ? 0 : old->second;
                    if (item->GetAmount() > count) {
                        grown.push_back({item->GetId(), item->GetAmount() - count});
                    }
                }
                try {
                    std::uint64_t added = 0;
                    for (const auto &[wuid, delta] : grown) {
                        auto *item       = inventory->FindOwnedItemById(wuid);
                        const auto known = _native.find(wuid);
                        if (delta != item->GetAmount() && (known == _native.end() || known->second.group != key)) {
                            throw std::runtime_error("The game merged items with different properties");
                        }
                        customItems.Bind(*item, properties);
                        if (properties.poisonBuff.IsValid() || properties.poisonCharges != 0 || !properties.onEquipBuffs.empty()) {
                            WriteProperties(*item, properties);
                        }
                        if (!Matches(*item, properties)) {
                            throw std::runtime_error(std::format("The game created {} with other properties than asked (quality {}, health {}, condition {}; expected {}, {}, {})", key.substr(0, key.find('\n')), item->InvokeGetQualityRaw(), item->GetHealth(),
                                item->InvokeGetConditionRaw(), properties.quality, properties.health, properties.condition));
                        }
                        added += delta;
                    }
                    if (added != missing) {
                        throw std::runtime_error(std::format("The game created {} of the {} asked for of {}", added, missing, key.substr(0, key.find('\n'))));
                    }
                    for (const auto &[wuid, delta] : grown) {
                        if (!_native.contains(wuid)) {
                            _native.insert_or_assign(wuid, NativeState {key, properties, {}, false});
                        }
                    }
                    group.retained += missing;
                    _failedKinds.erase(key);
                }
                catch (const std::exception &failure) {
                    // Only what this call added goes: a stack it merged into
                    // keeps the units it held before.
                    for (const auto &[wuid, delta] : grown) {
                        if (auto *item = inventory->FindOwnedItemById(wuid)) {
                            inventory->RemoveItem(item, SDK::wh::entitymodule::C_Inventory::kScriptRemovalReason, delta);
                        }
                    }
                    failedGroups.emplace(key, failure.what());
                    _failedKinds.insert_or_assign(key, FailedKind {group.units, failure.what()});
                }
            }

            // Hand each kind's rows out over its stacks, so a worn stack
            // knows which rows it wore.
            for (auto &[wuid, state] : _native) {
                auto *item           = inventory->FindOwnedItemById(wuid);
                auto &units          = groups.at(state.group).units;
                std::uint32_t needed = item->GetAmount();
                state.units.clear();
                for (auto &unit : units) {
                    const auto amount = std::min(unit.amount, needed);
                    if (amount == 0) {
                        continue;
                    }
                    state.units.push_back({unit.id, amount});
                    unit.amount -= amount;
                    needed -= amount;
                }
                if (needed != 0) {
                    throw std::runtime_error("The native inventory holds more than the server's rows");
                }
            }
            // What a failed kind's stacks could not hold is not shown; any
            // other row left without a stack means the bookkeeping is wrong.
            for (const auto &[key, group] : groups) {
                const auto failed = failedGroups.find(key);
                for (const auto &unit : group.units) {
                    if (unit.amount == 0) {
                        continue;
                    }
                    if (failed == failedGroups.end()) {
                        throw std::runtime_error("A server row has no native stack");
                    }
                    unshown.insert_or_assign(unit.id, failed->second);
                }
            }
            _initialized = true;
            error.clear();
            return true;
        }
        catch (const std::exception &failure) {
            // Nothing is permanent: the next apply starts over and adopts the
            // stacks that still match.
            error = failure.what();
            unshown.clear();
            _native.clear();
            _initialized = false;
            return false;
        }
    }

    bool NativeInventoryProjection::TakeWear(SDK::wh::entitymodule::C_Inventory *inventory, std::uint64_t wuid, std::vector<NativeWearClaim> &claims) {
        const auto known = _native.find(wuid);
        auto *item       = IsBoundTo(inventory) && known != _native.end() ? inventory->FindOwnedItemById(wuid) : nullptr;
        if (item == nullptr) {
            return false;
        }
        auto now = known->second.properties;
        try {
            ReadProperties(*item, now);
        }
        catch (const std::exception &) {
            return false;
        }
        if (Matches(*item, known->second.properties)) {
            return true;
        }
        if (!IsWear(known->second.properties, now)) {
            return false;
        }
        // Health, not condition: the server derives condition from it the
        // way the game did, so the two cannot disagree by a rounding.
        auto metadata = KCDC::Shared::Inventory::EncodeItemProperties(now);
        metadata.erase("condition");
        for (const auto &unit : known->second.units) {
            claims.push_back({unit.id, metadata});
        }
        known->second.properties  = now;
        known->second.wearPending = true;
        return true;
    }

    bool NativeInventoryProjection::TakeSharpening(SDK::wh::entitymodule::C_Inventory *inventory, std::uint64_t wuid, NativeRepairPart &part) const {
        const auto known = _native.find(wuid);
        auto *item       = IsBoundTo(inventory) && known != _native.end() ? inventory->FindOwnedItemById(wuid) : nullptr;
        if (item == nullptr || known->second.units.size() != 1 || known->second.units.front().amount != 1) {
            return false;
        }
        const auto &before = known->second.properties;
        auto now           = before;
        try {
            ReadProperties(*item, now);
        }
        catch (const std::exception &) {
            return false;
        }
        if (now.health <= before.health || now.quality != before.quality || now.poisonBuff != before.poisonBuff || now.poisonCharges != before.poisonCharges || now.onEquipBuffs != before.onEquipBuffs) {
            return false;
        }
        // Health, not condition, as for wear.
        part = {wuid, known->second.units.front().id, KCDC::Shared::Inventory::EncodeItemProperties(before), KCDC::Shared::Inventory::EncodeItemProperties(now), false, item->IsEquipped()};
        part.after.erase("condition");
        return true;
    }

    bool NativeInventoryProjection::ReadRepairStart(SDK::wh::entitymodule::C_Item &item, const std::vector<NativeRepairPart> &taken, NativeRepairPart &part) const {
        // The combine splits the unit it repairs off its stack, so the item
        // is usually one no apply has seen; it is placed by its properties.
        auto state = _native.find(item.GetId());
        if (state == _native.end()) {
            state = std::find_if(_native.begin(), _native.end(), [&item](const auto &entry) {
                return Matches(item, entry.second.properties);
            });
        }
        if (state == _native.end() || !Matches(item, state->second.properties)) {
            return false;
        }
        for (const auto &unit : UnitsOfKind(item)) {
            const auto claimed = std::count_if(taken.begin(), taken.end(), [&unit](const NativeRepairPart &other) {
                return other.rowId == unit.id;
            });
            if (unit.amount > static_cast<std::uint32_t>(claimed)) {
                part = {item.GetId(), unit.id, KCDC::Shared::Inventory::EncodeItemProperties(state->second.properties), nlohmann::json::object(), false};
                return true;
            }
        }
        return false;
    }

    bool NativeInventoryProjection::ReadRepairResult(SDK::wh::entitymodule::C_Item &item, bool kit, NativeRepairPart &part) {
        // CombineItems (0x182A68D3D) removes the kit when abs(health)
        // is at most 1e-5. A missing ID later can also mean it merged.
        if (kit && std::abs(item.GetHealth()) <= 0.00001F) {
            part.after           = part.before;
            part.after["health"] = 0.0F;
            part.after.erase("condition");
            part.spent = true;
            return true;
        }
        const auto itemClass = item.InvokeGetItemClassRaw();
        KCDC::Shared::RPC::InventoryItem properties;
        properties.inventoryItemId = 1;
        properties.itemClass       = {itemClass.m_low, itemClass.m_high};
        properties.amount          = 1;
        if (!KCDC::Shared::Inventory::ApplyItemProperties(part.before, properties)) {
            return false;
        }
        const auto effects = properties.onEquipBuffs;
        try {
            ReadProperties(item, properties);
        }
        catch (const std::exception &) {
            return false;
        }
        properties.onEquipBuffs = effects;
        // Health, not condition: the server derives one from the other.
        part.after = KCDC::Shared::Inventory::EncodeItemProperties(properties);
        part.after.erase("condition");
        return true;
    }

    std::vector<NativeUnit> NativeInventoryProjection::UnitsOfKind(SDK::wh::entitymodule::C_Item &item) const {
        std::vector<NativeUnit> units;
        const auto own = _native.find(item.GetId());
        auto state     = own;
        if (state == _native.end()) {
            state = std::find_if(_native.begin(), _native.end(), [&item](const auto &entry) {
                return Matches(item, entry.second.properties);
            });
        }
        if (state == _native.end()) {
            return units;
        }
        const auto add = [&units](const NativeUnit &unit) {
            const auto found = std::find_if(units.begin(), units.end(), [&unit](const NativeUnit &existing) {
                return existing.id == unit.id;
            });
            if (found == units.end()) {
                units.push_back(unit);
            }
            else {
                found->amount += unit.amount;
            }
        };
        if (own != _native.end()) {
            for (const auto &unit : own->second.units) {
                add(unit);
            }
        }
        for (auto entry = _native.begin(); entry != _native.end(); ++entry) {
            if (entry != own && entry->second.group == state->second.group) {
                for (const auto &unit : entry->second.units) {
                    add(unit);
                }
            }
        }
        return units;
    }

    std::vector<NativeConsumption> NativeInventoryProjection::TakeConsumption(SDK::wh::entitymodule::C_Inventory *inventory, const fu2::function<bool(std::uint64_t nativeId, const KCDC::Shared::RPC::ItemClassId &) const> &armed) {
        std::vector<NativeConsumption> spent;
        if (!IsBoundTo(inventory)) {
            return spent;
        }
        for (auto entry = _native.begin(); entry != _native.end();) {
            auto &state = entry->second;
            if (!armed(entry->first, state.properties.itemClass)) {
                ++entry;
                continue;
            }
            // A spent stack's wrapper may be gone: its id is only looked up.
            auto *item = inventory->FindOwnedItemById(entry->first);
            if (item != nullptr && (item->GetOwner() != inventory || !SameClass(*item, state.properties))) {
                ++entry;
                continue;
            }
            std::uint64_t projected = 0;
            for (const auto &unit : state.units) {
                projected += unit.amount;
            }
            const std::uint64_t remaining = item != nullptr ? item->GetAmount() : 0;
            if (remaining >= projected) {
                ++entry;
                continue;
            }
            // Spent from the back, so what is left keeps its leading rows.
            NativeConsumption consumption {state.properties.itemClass, {}};
            std::uint64_t owed = projected - remaining;
            for (auto unit = state.units.rbegin(); unit != state.units.rend() && owed != 0; ++unit) {
                const auto take = static_cast<std::uint32_t>(std::min<std::uint64_t>(unit->amount, owed));
                consumption.units.push_back({unit->id, take});
                unit->amount -= take;
                owed -= take;
            }
            std::erase_if(state.units, [](const NativeUnit &unit) {
                return unit.amount == 0;
            });
            spent.push_back(std::move(consumption));
            entry = remaining == 0 ? _native.erase(entry) : std::next(entry);
        }
        return spent;
    }

    void NativeInventoryProjection::Clear() {
        _native.clear();
        _lent.clear();
        _failedKinds.clear();
        _inventory   = nullptr;
        _initialized = false;
    }
} // namespace KCDC::Features::Items
