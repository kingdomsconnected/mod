#include "custom_item_module.h"
#include "inventory_use_service.h"
#include "core/application.h"
#include "core/local_events.h"
#include "core/obfuscation.h"
#include "features/items/custom_item_presentation.h"
#include "features/ui/ui_image_module.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_item.h"
#include "sdk/cryengine/action/i_item_system.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/wh/entitymodule/c_entity_module.h"
#include "sdk/wh/entitymodule/c_item_database.h"
#include "sdk/wh/entitymodule/s_ammo_item_class.h"
#include "sdk/wh/entitymodule/s_armor_item_class.h"
#include "sdk/wh/entitymodule/s_melee_weapon_item_class.h"
#include "sdk/wh/entitymodule/s_missile_weapon_item_class.h"
#include "sdk/wh/entitymodule/s_player_item_class.h"
#include "shared/features/inventory/inventory_message.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/item_classes.h"
#include "shared/game/item_stats.h"
#include <MinHook.h>
#include <algorithm>
#include <cmath>
#include <core_modules.h>
#include <format>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <utils/hooking/hook_function.h>
#include <utils/safe_win32.h>

namespace KCDC::Features::Items {
    namespace {
        SDK::wh::entitymodule::S_ItemClassGUID NativeGuid(const std::string &text) {
            const auto id = KCDC::Shared::Game::ItemClassIdFromGuid(text);
            return {id.low, id.high};
        }
        std::string GuidOf(const SDK::wh::entitymodule::C_Item &item) {
            const auto &id = item.GetItemClass()->GetGuid();
            return KCDC::Shared::Game::CryGuidToString(id.m_low, id.m_high);
        }
    } // namespace

    CustomItemModule &CustomItemModule::Get() {
        static CustomItemModule instance;
        return instance;
    }

    void CustomItemModule::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::CustomItemDefinitions>([](const KCDC::Shared::RPC::CustomItemDefinitions &message, MafiaNet::Packet *) {
            if (!message.valid) {
                return;
            }
            auto &module = Get();
            std::scoped_lock lock(module._mutex);
            if (module._incoming.size() >= 16384) {
                module._incoming.clear();
                module._incoming.push_back("{}");
                return;
            }
            module._incoming.push_back(message.body);
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::ItemStats>([](const KCDC::Shared::RPC::ItemStats &table, MafiaNet::Packet *) {
            if (!table.IsSane()) {
                return;
            }
            auto &module = Get();
            std::scoped_lock lock(module._mutex);
            // A whole table makes everything queued before it moot; a later change to a class
            // supersedes an earlier one still queued. Bounded by the classes there are, so a burst
            // of updates while the game thread is busy loads is never dropped.
            if (table.replace) {
                module._incomingStatTable = table;
                module._incomingStatUpdates.clear();
                return;
            }
            for (const auto &entry : table.entries) {
                module._incomingStatUpdates.insert_or_assign(KCDC::Shared::Game::CryGuidToString(entry.itemClass.low, entry.itemClass.high), entry.values);
            }
        });
    }

    namespace {
        /** The typed row behind a class, or null when the row is not of `classId`: the catalog's category is a claim, the row's own class test is the proof. */
        template <typename T>
        T *RowAs(SDK::wh::entitymodule::S_ItemClass &row, SDK::wh::entitymodule::E_ItemClassId classId) {
            return row.IsOfClass(classId) ? static_cast<T *>(&row) : nullptr;
        }

        /** Reads or writes one stat of one class. `value` is read when `write` is false. False for a stat the element does not carry. */
        bool AccessItemStat(SDK::wh::entitymodule::S_ItemClass &row, std::string_view category, KCDC::Shared::Game::ItemStat stat, float &value, bool write) {
            if (!KCDC::Shared::Game::ItemStatAppliesTo(category, stat)) {
                return false;
            }
            const auto access = [&](auto getter, auto setter) {
                if (write) {
                    setter(value);
                }
                else {
                    value = static_cast<float>(getter());
                }
                return true;
            };
            if (category == "MeleeWeapon") {
                auto *melee = RowAs<SDK::wh::entitymodule::S_MeleeWeaponItemClass>(row, SDK::wh::entitymodule::E_ItemClassId::MeleeWeapon);
                if (melee == nullptr) {
                    return false;
                }
                switch (stat) {
                case KCDC::Shared::Game::ItemStat::Attack: return access([&] { return melee->GetAttack(); }, [&](float v) { melee->SetAttack(v); });
                case KCDC::Shared::Game::ItemStat::AttackModStab: return access([&] { return melee->GetAttackModStab(); }, [&](float v) { melee->SetAttackModStab(v); });
                case KCDC::Shared::Game::ItemStat::AttackModSlash: return access([&] { return melee->GetAttackModSlash(); }, [&](float v) { melee->SetAttackModSlash(v); });
                case KCDC::Shared::Game::ItemStat::AttackModSmash: return access([&] { return melee->GetAttackModSmash(); }, [&](float v) { melee->SetAttackModSmash(v); });
                case KCDC::Shared::Game::ItemStat::Defense: return access([&] { return melee->GetDefense(); }, [&](float v) { melee->SetDefense(v); });
                case KCDC::Shared::Game::ItemStat::StrReq: return access([&] { return melee->GetStrengthRequirement(); }, [&](float v) { melee->SetStrengthRequirement(static_cast<std::uint32_t>(v)); });
                case KCDC::Shared::Game::ItemStat::AgiReq: return access([&] { return melee->GetAgilityRequirement(); }, [&](float v) { melee->SetAgilityRequirement(static_cast<std::uint32_t>(v)); });
                default: return false;
                }
            }
            if (category == "MissileWeapon") {
                auto *missile = RowAs<SDK::wh::entitymodule::S_MissileWeaponItemClass>(row, SDK::wh::entitymodule::E_ItemClassId::MissileWeapon);
                if (missile == nullptr) {
                    return false;
                }
                switch (stat) {
                case KCDC::Shared::Game::ItemStat::Power: return access([&] { return missile->GetPower(); }, [&](float v) { missile->SetPower(v); });
                case KCDC::Shared::Game::ItemStat::LimbResistance: return access([&] { return missile->GetLimbResistance(); }, [&](float v) { missile->SetLimbResistance(v); });
                case KCDC::Shared::Game::ItemStat::Defense: return access([&] { return missile->GetDefense(); }, [&](float v) { missile->SetDefense(v); });
                case KCDC::Shared::Game::ItemStat::StrReq: return access([&] { return missile->GetStrengthRequirement(); }, [&](float v) { missile->SetStrengthRequirement(static_cast<std::uint32_t>(v)); });
                case KCDC::Shared::Game::ItemStat::AgiReq: return access([&] { return missile->GetAgilityRequirement(); }, [&](float v) { missile->SetAgilityRequirement(static_cast<std::uint32_t>(v)); });
                default: return false;
                }
            }
            if (category == "Armor" || category == "Helmet" || category == "Hood") {
                auto *armor = RowAs<SDK::wh::entitymodule::S_ArmorItemClass>(row, SDK::wh::entitymodule::E_ItemClassId::Armor);
                if (armor == nullptr) {
                    return false;
                }
                switch (stat) {
                case KCDC::Shared::Game::ItemStat::DefenseStab: return access([&] { return armor->GetDefenseStab(); }, [&](float v) { armor->SetDefenseStab(v); });
                case KCDC::Shared::Game::ItemStat::DefenseSlash: return access([&] { return armor->GetDefenseSlash(); }, [&](float v) { armor->SetDefenseSlash(v); });
                case KCDC::Shared::Game::ItemStat::DefenseSmash: return access([&] { return armor->GetDefenseSmash(); }, [&](float v) { armor->SetDefenseSmash(v); });
                case KCDC::Shared::Game::ItemStat::StrReq: return access([&] { return armor->GetStrengthRequirement(); }, [&](float v) { armor->SetStrengthRequirement(v); });
                case KCDC::Shared::Game::ItemStat::Noise: return access([&] { return armor->GetNoise(); }, [&](float v) { armor->SetNoise(v); });
                default: return false;
                }
            }
            if (category == "Ammo") {
                auto *ammo = RowAs<SDK::wh::entitymodule::S_AmmoItemClass>(row, SDK::wh::entitymodule::E_ItemClassId::Ammo);
                if (ammo == nullptr) {
                    return false;
                }
                switch (stat) {
                case KCDC::Shared::Game::ItemStat::AttackStab: return access([&] { return ammo->GetAttackStab(); }, [&](float v) { ammo->SetAttackStab(v); });
                case KCDC::Shared::Game::ItemStat::AttackSlash: return access([&] { return ammo->GetAttackSlash(); }, [&](float v) { ammo->SetAttackSlash(v); });
                case KCDC::Shared::Game::ItemStat::AttackSmash: return access([&] { return ammo->GetAttackSmash(); }, [&](float v) { ammo->SetAttackSmash(v); });
                case KCDC::Shared::Game::ItemStat::PowerMod: return access([&] { return ammo->GetPowerMod(); }, [&](float v) { ammo->SetPowerMod(v); });
                default: return false;
                }
            }
            return false;
        }
    } // namespace

    bool CustomItemModule::WriteItemStats(const KCDC::Shared::RPC::ItemClassId &itemClass, const std::vector<KCDC::Shared::RPC::ItemStats::Value> &values) {
        const auto *entry = KCDC::Shared::Game::FindItemClass(itemClass);
        auto *database    = SDK::wh::entitymodule::C_ItemDatabase::GetInstance();
        auto *row         = database != nullptr ? database->FindMutableItemClass({itemClass.low, itemClass.high}) : nullptr;
        if (entry == nullptr || row == nullptr || !KCDC::Shared::Game::ItemCategoryHasStats(entry->category)) {
            return false;
        }
        const std::string guid(entry->guid);
        auto &originals = _statOriginals[guid];

        // Everything the server wrote before and does not write now goes back
        // to what the table said, so a shrinking override is a restoring one.
        for (std::size_t i = 0; i < originals.values.size(); ++i) {
            if (!originals.values[i].has_value()) {
                continue;
            }
            const bool kept = std::any_of(values.begin(), values.end(), [i](const auto &value) { return value.stat == i; });
            if (!kept) {
                float authored = *originals.values[i];
                AccessItemStat(*row, entry->category, static_cast<KCDC::Shared::Game::ItemStat>(i), authored, true);
                originals.values[i].reset();
            }
        }
        for (const auto &value : values) {
            const auto stat = static_cast<KCDC::Shared::Game::ItemStat>(value.stat);
            if (!KCDC::Shared::Game::ItemStatAppliesTo(entry->category, stat)) {
                continue;
            }
            auto &original = originals.values[value.stat];
            if (!original.has_value()) {
                float current = 0.0F;
                if (!AccessItemStat(*row, entry->category, stat, current, false)) {
                    continue;
                }
                original = current;
            }
            float written = KCDC::Shared::Game::ItemStatIsIntegral(stat) ? std::round(value.value) : value.value;
            AccessItemStat(*row, entry->category, stat, written, true);
        }
        if (values.empty()) {
            _statOverrides.erase(guid);
            _statOriginals.erase(guid);
        }
        else {
            _statOverrides[guid] = values;
        }
        return true;
    }

    void CustomItemModule::ApplyItemStats(const KCDC::Shared::RPC::ItemStats &table) {
        if (table.replace) {
            _desiredStats.clear();
        }
        for (const auto &entry : table.entries) {
            const std::string guid = KCDC::Shared::Game::CryGuidToString(entry.itemClass.low, entry.itemClass.high);
            if (entry.values.empty()) {
                _desiredStats.erase(guid);
            }
            else {
                _desiredStats.insert_or_assign(guid, entry.values);
            }
        }
        if (table.replace) {
            // Classes the new table leaves out are given their numbers back.
            std::vector<std::string> dropped;
            for (const auto &[guid, values] : _statOverrides) {
                const auto id = KCDC::Shared::Game::ItemClassIdFromGuid(guid);
                if (std::none_of(table.entries.begin(), table.entries.end(), [&id](const auto &entry) { return entry.itemClass == id; })) {
                    dropped.push_back(guid);
                }
            }
            for (const auto &guid : dropped) {
                WriteItemStats(KCDC::Shared::Game::ItemClassIdFromGuid(guid), {});
            }
        }
        std::size_t written = 0;
        for (const auto &entry : table.entries) {
            if (WriteItemStats(entry.itemClass, entry.values)) {
                ++written;
            }
            else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Stat override for item class {} not written: the item database has no equippable class by that id yet; it is written if a reload brings one", KCDC::Shared::Game::CryGuidToString(entry.itemClass.low, entry.itemClass.high));
            }
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Applied item stat overrides: {} class(es) in this {}, {} class(es) now overridden", written, table.replace ? "table" : "update", _statOverrides.size());
    }

    void CustomItemModule::RestoreItemStats() {
        std::vector<std::string> overridden;
        for (const auto &[guid, values] : _statOverrides) {
            overridden.push_back(guid);
        }
        for (const auto &guid : overridden) {
            WriteItemStats(KCDC::Shared::Game::ItemClassIdFromGuid(guid), {});
        }
        _statOverrides.clear();
        _statOriginals.clear();
        _desiredStats.clear();
    }

    std::optional<std::vector<KCDC::Shared::RPC::ItemStats::Value>> CustomItemModule::ItemStatOverrides(const KCDC::Shared::RPC::ItemClassId &itemClass) const {
        const auto found = _statOverrides.find(KCDC::Shared::Game::CryGuidToString(itemClass.low, itemClass.high));
        if (found == _statOverrides.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    void CustomItemModule::Update() {
        // FindItem covers every holder, including stock, stashes and ground
        // items. Never retain or dereference a retired native pointer.
        auto *entities = SDK::wh::entitymodule::C_EntityModule::GetInstance();
        auto *items    = entities != nullptr ? entities->GetItemManager() : nullptr;
        if (items != nullptr) {
            std::scoped_lock lock(_mutex);
            _bindings.Prune([items](std::uint64_t id, const std::string &nativeGuid) {
                const auto *item = items->FindItem(SDK::wh::framework::WUID {id});
                return item != nullptr && item->GetItemClass() != nullptr && GuidOf(*item) == nativeGuid;
            });
        }
        // The feature sweep reaches this before any inventory projection.
        auto *database = SDK::wh::entitymodule::C_ItemDatabase::GetInstance();
        if (SDK::ISystem::GetInstance() == nullptr || database->ObjectCount() == 0) {
            return;
        }
        std::vector<std::string> messages;
        std::optional<KCDC::Shared::RPC::ItemStats> statTable;
        KCDC::Shared::RPC::ItemStats statUpdates;
        {
            std::scoped_lock lock(_mutex);
            messages.swap(_incoming);
            statTable.swap(_incomingStatTable);
            for (auto &[guid, values] : _incomingStatUpdates) {
                statUpdates.entries.push_back({KCDC::Shared::Game::ItemClassIdFromGuid(guid), std::move(values)});
            }
            _incomingStatUpdates.clear();
        }
        if (statTable) {
            ApplyItemStats(*statTable);
        }
        if (!statUpdates.entries.empty()) {
            ApplyItemStats(statUpdates);
        }
        for (const auto &body : messages) {
            try {
                const auto message = nlohmann::json::parse(body);
                if (message.value("begin", false)) {
                    _complete = false;
                    if (message.value("reset", false)) {
                        KCDC::Shared::Game::CustomItemRegistry::Get().Reset();
                        std::scoped_lock lock(_mutex);
                        _nativeToLogical.clear();
                        _bindings.Reset();
                    }
                }
                else if (message.contains("id")) {
                    const auto &definition = KCDC::Shared::Game::CustomItemRegistry::Get().Register(message.at("id").get<std::string>(), message.at("definition"));
                    if (!RegisterNative(definition)) {
                        throw std::runtime_error("Native item definition rejected: " + definition.id);
                    }
                }
                else if (message.value("end", false)) {
                    if (message.at("revision").get<std::uint64_t>() != KCDC::Shared::Game::CustomItemRegistry::Get().Revision()) {
                        throw std::runtime_error("Incomplete item registry");
                    }
                    _complete = true;
                    KCDC::Core::gApplication->GetPlayerInventoryService().DefinitionsChanged();
                }
                else {
                    throw std::runtime_error("Invalid item registry message");
                }
            }
            catch (const std::exception &error) {
                _failure  = error.what();
                _complete = false;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Item registration failed: {}", _failure);
                KCDC::Core::gApplication->GetPlayerInventoryService().DefinitionsChanged();
            }
        }
    }

    bool CustomItemModule::RegisterNative(const KCDC::Shared::Game::CustomItemDefinition &definition) {
        auto *database        = SDK::wh::entitymodule::C_ItemDatabase::GetInstance();
        const auto nativeGuid = NativeGuid(definition.nativeGuid);
        {
            std::scoped_lock lock(_mutex);
            _labels[KCDC::Features::Items::CustomItemPresentation::TypeKey(definition.nativeGuid, "n")] = definition.name;
            _labels[KCDC::Features::Items::CustomItemPresentation::TypeKey(definition.nativeGuid, "d")] = definition.description;
            _labels[KCDC::Features::Items::CustomItemPresentation::TypeKey(definition.nativeGuid, "u")] = definition.useLabel;
        }
        if (!_installed.contains(definition.nativeGuid)) {
            // Only the appearance is borrowed, and only what the definition
            // does not bring itself; without a visual it brings a model and an
            // icon. Generic broken-arrow script supplies the MiscItem entity
            // script, without Book's reading effects.
            const auto *visual    = definition.visual.empty() ? nullptr : database->FindItemClass(NativeGuid(definition.visual));
            const auto *prototype = database->FindItemClass(NativeGuid("3b5be706-3cac-4223-a780-039867c65c4d"));
            if (prototype == nullptr || !prototype->IsOfClass(SDK::wh::entitymodule::E_ItemClassId::PlayerItem) || (!definition.visual.empty() && (visual == nullptr || !visual->IsOfClass(SDK::wh::entitymodule::E_ItemClassId::PlayerItem)))) {
                return false;
            }
            const auto *appearance = static_cast<const SDK::wh::entitymodule::S_PlayerItemClass *>(visual);
            const auto *generic    = static_cast<const SDK::wh::entitymodule::S_PlayerItemClass *>(prototype);
            // A preexisting GUID not installed by us is a collision, never
            // permission to replace a class existing objects reference.
            if (database->FindItemClass(nativeGuid) != nullptr) {
                return false;
            }
            // An icon is decoded once however many classes carry it, and one that
            // fails is not tried again: its IconId is its bytes. The server checks
            // only that the PNG's chunks hold together, so image data WIC cannot
            // inflate still arrives: those classes show their visual's icon, or
            // the generic item's, rather than failing the registry and with it
            // every inventory projection.
            const char *iconId = (appearance != nullptr ? appearance : generic)->GetIconId();
            if (!definition.iconId.empty() && !_undecodableIcons.contains(definition.iconId)) {
                bool decoded = _servedIcons.contains(definition.iconId);
                if (!decoded) {
                    // The UI asks for `img://Libs/UI/Textures/Icons/Items/<IconId>_icon.dds`.
                    if (auto pixels = KCDC::Features::UI::UiImageModule::Decode(definition.iconPng, KCDC::Shared::Game::kCustomItemIconSide)) {
                        KCDC::Features::UI::UiImageModule::Get().Serve("Libs/UI/Textures/Icons/Items/" + definition.iconId + "_icon.dds", std::move(pixels));
                        _servedIcons.insert(definition.iconId);
                        decoded = true;
                    }
                    else {
                        _undecodableIcons.insert(definition.iconId);
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Custom item {}: icon {} is not a PNG this client can decode; every class carrying it shows its visual's icon, or the generic item's", definition.id, definition.iconId);
                    }
                }
                if (decoded) {
                    iconId = definition.iconId.c_str();
                }
            }
            constexpr char xml[] = "<database><ItemClasses version=\"8\"><MiscItem/></ItemClasses></database>";
            const auto root      = SDK::ISystem::GetInstance()->LoadXmlFromBuffer(xml, sizeof(xml) - 1, false, false);
            if (!root.IsValid()) {
                return false;
            }
            const auto row = root->getChild(0)->getChild(0);
            const auto set = [&row](const char *key, const char *value) { row->setAttr(key, value != nullptr ? value : ""); };
            set("Id", definition.nativeGuid.c_str());
            set("Name", KCDC::Features::Items::CustomItemPresentation::TypeKey(definition.nativeGuid, "internal").c_str());
            set("Model", definition.model.empty() ? appearance->GetModel() : definition.model.c_str());
            set("Material", definition.model.empty() ? appearance->GetMaterial() : definition.material.c_str());
            set("IconId", iconId);
            set("EntityScript", generic->GetEntityScript());
            set("UIName", KCDC::Features::Items::CustomItemPresentation::TypeLabel(definition.nativeGuid, "n").c_str());
            set("UIInfo", KCDC::Features::Items::CustomItemPresentation::TypeLabel(definition.nativeGuid, "d").c_str());
            set("Weight", std::format("{:.9g}", definition.weight).c_str());
            set("Price", std::to_string(definition.price).c_str());
            set("IsDivisible", definition.stackable ? "true" : "false");
            set("IsQuestItem", "false");
            if (!database->AppendFromXml(root, nullptr, false) || !database->RebuildIndex() || database->FindItemClass(nativeGuid) == nullptr) {
                return false;
            }
            std::scoped_lock lock(_mutex);
            _installed.insert(definition.nativeGuid);
        }
        const auto *registered = database->FindItemClass(nativeGuid);
        if (registered == nullptr || !registered->IsOfClass(SDK::wh::entitymodule::E_ItemClassId::PlayerItem) || registered->IsDivisible() != definition.stackable) {
            return false;
        }
        const auto *playerClass = static_cast<const SDK::wh::entitymodule::S_PlayerItemClass *>(registered);
        if (playerClass->IsQuestItem() || playerClass->GetWeight() != definition.weight || playerClass->GetPrice() != static_cast<std::int32_t>(definition.price)) {
            return false;
        }
        std::scoped_lock lock(_mutex);
        _nativeToLogical[definition.nativeGuid] = definition.guid;
        return true;
    }

    void CustomItemModule::OnItemDatabaseReloaded() {
        // The stat overrides went with the old rows, and the new ones hold the tables' numbers, a
        // mod's patches included: nothing written is on them, and what was captured as authored is
        // stale. The server's table is written again from scratch, a streamed mod's classes with it
        // now that their rows exist.
        _statOverrides.clear();
        _statOriginals.clear();
        std::size_t rewritten = 0;
        for (const auto &[guid, values] : _desiredStats) {
            rewritten += WriteItemStats(KCDC::Shared::Game::ItemClassIdFromGuid(guid), values) ? 1 : 0;
        }
        if (!_desiredStats.empty()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Item database reloaded: stat overrides written again on {} of {} class(es)", rewritten, _desiredStats.size());
        }

        // The item database's Load clears its rows before it reads the tables back (0x1816A369C),
        // so nothing appended survives: not this session's classes, nor an earlier one's. Their
        // icons go too, but for the ones this registry's classes name again; an image the UI
        // already holds keeps its own pixels.
        std::set<std::string, std::less<>> named;
        for (const auto &[guid, definition] : KCDC::Shared::Game::CustomItemRegistry::Get().Definitions()) {
            named.insert(definition.iconId);
        }
        {
            std::scoped_lock lock(_mutex);
            _installed.clear();
        }
        std::erase_if(_servedIcons, [&named](const std::string &iconId) {
            if (named.contains(iconId)) {
                return false;
            }
            KCDC::Features::UI::UiImageModule::Get().Withdraw("Libs/UI/Textures/Icons/Items/" + iconId + "_icon.dds");
            return true;
        });
        std::erase_if(_undecodableIcons, [&named](const std::string &iconId) {
            return !named.contains(iconId);
        });
        std::size_t restored = 0;
        for (const auto &[guid, definition] : KCDC::Shared::Game::CustomItemRegistry::Get().Definitions()) {
            if (!RegisterNative(definition)) {
                _failure  = "Native item definition rejected after a database reload: " + definition.id;
                _complete = false;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Item registration failed: {}", _failure);
                KCDC::Core::gApplication->GetPlayerInventoryService().DefinitionsChanged();
                return;
            }
            ++restored;
        }
        if (restored != 0) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Item database reloaded: {} custom item class(es) registered again", restored);
        }
    }

    void CustomItemModule::Reset() {
        // The rows outlive the session; the next server starts from the authored numbers.
        RestoreItemStats();
        std::scoped_lock lock(_mutex);
        _incoming.clear();
        _incomingStatTable.reset();
        _incomingStatUpdates.clear();
        _bindings.Reset();
        _nativeToLogical.clear();
        _complete = false;
        _failure.clear();
        KCDC::Shared::Game::CustomItemRegistry::Get().Reset();
        // Native classes and their type labels survive until process exit.
    }

    SDK::wh::entitymodule::S_ItemClassGUID CustomItemModule::ToNative(const KCDC::Shared::RPC::ItemClassId &id) const {
        return KCDC::Features::Items::CustomItemPresentation::ToNative(id);
    }

    KCDC::Shared::RPC::ItemClassId CustomItemModule::ToLogical(const SDK::wh::entitymodule::S_ItemClassGUID &id) const {
        std::scoped_lock lock(_mutex);
        const auto found = _nativeToLogical.find(KCDC::Shared::Game::CryGuidToString(id.m_low, id.m_high));
        return found == _nativeToLogical.end() ? KCDC::Shared::RPC::ItemClassId {id.m_low, id.m_high} : KCDC::Shared::Game::ItemClassIdFromGuid(found->second);
    }

    bool CustomItemModule::IsCustom(const SDK::wh::entitymodule::C_Item *item) const {
        if (item == nullptr || item->GetItemClass() == nullptr) {
            return false;
        }
        std::scoped_lock lock(_mutex);
        return _installed.contains(GuidOf(*item));
    }

    void CustomItemModule::Bind(SDK::wh::entitymodule::C_Item &item, const KCDC::Shared::RPC::InventoryItem &properties) {
        const auto *definition = KCDC::Shared::Game::CustomItemRegistry::Get().Find(KCDC::Shared::Game::CryGuidToString(properties.itemClass.low, properties.itemClass.high));
        if (definition == nullptr) {
            return;
        }
        bool changed = false;
        {
            std::scoped_lock lock(_mutex);
            changed = _bindings.Bind(item.GetId(), definition->nativeGuid, properties.custom, properties.customStackKey, properties.custom.value("name", definition->name), properties.custom.value("description", definition->description), definition->stackable);
        }
        // Creation can publish its UI row before instance properties arrive.
        // The initial binding needs the same refresh as a later rename.
        if (changed) {
            item.NotifyChanged();
        }
    }

    void CustomItemModule::Read(const SDK::wh::entitymodule::C_Item &item, KCDC::Shared::RPC::InventoryItem &properties) const {
        std::scoped_lock lock(_mutex);
        properties.custom = nlohmann::json::object();
        properties.customStackKey.clear();
        if (const auto *binding = _bindings.Find(item.GetId(), GuidOf(item))) {
            properties.custom         = binding->custom;
            properties.customStackKey = binding->stackKey;
        }
    }

    bool CustomItemModule::Matches(const SDK::wh::entitymodule::C_Item &item, const KCDC::Shared::RPC::InventoryItem &properties) const {
        if (!IsCustom(&item)) {
            return properties.custom.empty() && properties.customStackKey.empty();
        }
        KCDC::Shared::RPC::InventoryItem current;
        Read(item, current);
        return current.custom == properties.custom && current.customStackKey == properties.customStackKey;
    }

    bool CustomItemModule::AllowMerge(const SDK::wh::entitymodule::C_Item *left, const SDK::wh::entitymodule::C_Item *right) const {
        if (left == nullptr || right == nullptr) {
            return true;
        }
        std::scoped_lock lock(_mutex);
        if (!_installed.contains(GuidOf(*left)) && !_installed.contains(GuidOf(*right))) {
            return true;
        }
        const auto *a = _bindings.Find(left->GetId(), GuidOf(*left));
        const auto *b = _bindings.Find(right->GetId(), GuidOf(*right));
        // Unbound new items never merge before their properties are assigned.
        return a != nullptr && b != nullptr && a->nativeGuid == b->nativeGuid && a->custom == b->custom && a->stackKey == b->stackKey;
    }

    void CustomItemModule::CopyBinding(const SDK::wh::entitymodule::C_Item &source, const SDK::wh::entitymodule::C_Item &destination) {
        std::scoped_lock lock(_mutex);
        if (GuidOf(source) == GuidOf(destination)) {
            _bindings.Copy(source.GetId(), destination.GetId(), GuidOf(source));
        }
    }

    std::string CustomItemModule::Label(const SDK::wh::entitymodule::C_Item &item, bool description) const {
        std::scoped_lock lock(_mutex);
        return _bindings.Find(item.GetId(), GuidOf(item)) != nullptr ? KCDC::Features::Items::CustomItemPresentation::InstanceLabel(item.GetId(), description) : KCDC::Features::Items::CustomItemPresentation::TypeLabel(GuidOf(item), description ? "d" : "n");
    }

    bool CustomItemModule::ResolveLabel(const char *label, SDK::CryStringT<char> &out) const {
        std::string_view key(label);
        if (key.starts_with('@')) {
            key.remove_prefix(1);
        }
        std::string text;
        {
            std::scoped_lock lock(_mutex);
            if (!_bindings.ResolveLabel(label, text)) {
                const auto found = _labels.find(key);
                if (found == _labels.end()) {
                    return false;
                }
                text = found->second;
            }
        }
        out.Assign(text.c_str());
        return true;
    }

    bool CustomItemModule::Usable(const SDK::wh::entitymodule::C_Item *item, std::string *label) const {
        if (!Ready() || !IsCustom(item)) {
            return false;
        }
        const auto *definition = KCDC::Shared::Game::CustomItemRegistry::Get().FindNative(GuidOf(*item));
        if (definition == nullptr || !definition->usable) {
            return false;
        }
        if (label != nullptr) {
            *label = KCDC::Features::Items::CustomItemPresentation::TypeLabel(definition->nativeGuid, "u");
        }
        return true;
    }

    SDK::wh::guimodule::C_UIFlashListItem *CustomItemModule::ListConstruct(SDK::wh::guimodule::C_UIFlashListItem *self, SDK::wh::entitymodule::C_Item *item, void *context) {
        auto &module = Get();
        auto *result = module._listOriginal(self, item, context);
        if (module.IsCustom(item)) {
            self->SetName(module.Label(*item).c_str());
            std::scoped_lock lock(module._mutex);
            if (const auto id = module._bindings.ListClassId(item->GetId(), GuidOf(*item))) {
                self->SetClassId(*id);
            }
        }
        return result;
    }

    SDK::wh::guimodule::C_UIFlashInfoItem *CustomItemModule::InfoConstruct(SDK::wh::guimodule::C_UIFlashInfoItem *self, SDK::wh::entitymodule::C_Item *item, void *context) {
        auto &module = Get();
        auto *result = module._infoOriginal(self, item, context);
        if (module.IsCustom(item)) {
            self->SetName(module.Label(*item).c_str());
            self->SetDescription(module.Label(*item, true).c_str());
        }
        return result;
    }

    KCDC::Features::Items::NativeInventoryActionHint *CustomItemModule::BuildHint(void *self, KCDC::Features::Items::NativeInventoryActionHint *out, SDK::wh::entitymodule::C_Item *item) {
        auto &module = Get();
        auto *result = module._hintOriginal(self, out, item);
        if (InventoryUseService::Get().HasHandler(item) && KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerInventoryService().ManagesNativeItem(item)) {
            out->visible = true;
            out->enabled = KCDC::Core::gApplication->GetPlayerInventoryService().CanSpend();
            out->label.Assign("ui_invhint_use");
            out->reason.Assign("");
        }
        else if (module.IsCustom(item)) {
            std::string label;
            out->visible = module.Usable(item, &label);
            out->enabled = out->visible && KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetPlayerInventoryService().CanSpend() && KCDC::Core::gApplication->GetPlayerInventoryService().ManagesNativeItem(item);
            out->label.Assign(label.c_str());
            out->reason.Assign("");
        }
        return result;
    }

    void CustomItemModule::Activate(void *self, SDK::wh::entitymodule::C_Item *item) {
        if (KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetLocalEvents().InventoryItemUsed(item, "activate")) {
            return;
        }
        if (Get().IsCustom(item)) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().CustomItemUsed(item);
            }
            return;
        }
        Get()._activateOriginal(self, item);
    }

    void CustomItemModule::InvokeAction(void *self, const std::vector<SDK::wh::entitymodule::C_Item *> &items, unsigned int amount) {
        if (!items.empty() && KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetLocalEvents().InventoryItemUsed(items.front(), "primary")) {
            return;
        }
        if (!items.empty() && Get().IsCustom(items.front())) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetLocalEvents().CustomItemUsed(items.front());
            }
            return;
        }
        Get()._actionOriginal(self, items, amount);
    }

    void CustomItemModule::InvokeSecondaryAction(void *self, const std::vector<SDK::wh::entitymodule::C_Item *> &items, unsigned int amount) {
        if (!items.empty() && KCDC::Core::gApplication != nullptr && KCDC::Core::gApplication->GetLocalEvents().InventoryItemUsed(items.front(), "secondary")) {
            return;
        }
        Get()._secondaryActionOriginal(self, items, amount);
    }

    SDK::wh::entitymodule::C_Item *CustomItemModule::Clone(const SDK::wh::entitymodule::C_Item *self, SDK::wh::entitymodule::C_ItemHolder *holder) {
        auto &module = Get();
        auto *result = module._cloneOriginal(self, holder);
        if (result != nullptr) {
            module.CopyBinding(*self, *result);
        }
        return result;
    }

    int CustomItemModule::PickupName(void *self, SDK::IFunctionHandler *handler) {
        const auto id   = handler->GetThis();
        auto *framework = SDK::IGameFramework::GetInstance();
        auto *items     = framework != nullptr ? framework->GetIItemSystem() : nullptr;
        auto *pickable  = items != nullptr && id <= UINT32_MAX ? items->GetItem(static_cast<SDK::EntityId>(id)) : nullptr;
        auto *item      = pickable != nullptr ? pickable->GetRpgItem() : nullptr;
        if (Get().IsCustom(item)) {
            const auto label = Get().Label(*item);
            return handler->EndFunctionAny(SDK::ScriptAnyValue(label.c_str()));
        }
        return Get()._pickupOriginal(self, handler);
    }

    void CustomItemModule::InstallHooks() {
        auto &module = Get();
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIFlashListItem__Construct), reinterpret_cast<LPVOID>(&ListConstruct), reinterpret_cast<LPVOID *>(&module._listOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIFlashInfoItem__Construct), reinterpret_cast<LPVOID>(&InfoConstruct), reinterpret_cast<LPVOID *>(&module._infoOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIApseInventory__BuildActionHint), reinterpret_cast<LPVOID>(&BuildHint), reinterpret_cast<LPVOID *>(&module._hintOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIApseInventory__ActivateItem), reinterpret_cast<LPVOID>(&Activate), reinterpret_cast<LPVOID *>(&module._activateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.InventoryActionCallback__Invoke), reinterpret_cast<LPVOID>(&InvokeAction), reinterpret_cast<LPVOID *>(&module._actionOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.InventorySecondaryActionCallback__Invoke), reinterpret_cast<LPVOID>(&InvokeSecondaryAction), reinterpret_cast<LPVOID *>(&module._secondaryActionOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Item__CloneForHolder), reinterpret_cast<LPVOID>(&Clone), reinterpret_cast<LPVOID *>(&module._cloneOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ScriptBindPickableItem__GetUIName), reinterpret_cast<LPVOID>(&PickupName), reinterpret_cast<LPVOID *>(&module._pickupOriginal));
    }
} // namespace KCDC::Features::Items

static InitFunction init([] { KCDC::Features::Items::CustomItemModule::InstallHooks(); }, "Item presentation");
