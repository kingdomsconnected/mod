#include "native_item_effects.h"
#include "features/items/custom_item_module.h"

#include "sdk/wh/entitymodule/c_missile_weapon_runtime_data.h"
#include "shared/game/cry_guid_text.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/inventory_property_policy.h"

#include <algorithm>

namespace KCDC::Features::Items {
    bool ReadNativeItemEffects(const SDK::wh::entitymodule::C_Item &item, std::vector<KCDC::Shared::RPC::ItemClassId> &effects, std::string &error) {
        if (KCDC::Features::Items::CustomItemModule::Get().IsCustom(&item)) {
            effects.clear();
            error.clear();
            return true;
        }
        const auto itemClass   = item.InvokeGetItemClassRaw();
        const auto *definition = KCDC::Shared::Game::FindInventoryPropertyClass(KCDC::Shared::Game::CryGuidToString(itemClass.m_low, itemClass.m_high));
        if (definition == nullptr) {
            error = "Unknown native item class while reading item effects";
            return false;
        }
        std::vector<KCDC::Shared::RPC::ItemClassId> captured;
        if (definition->equippable) {
            const auto *runtime = static_cast<const SDK::wh::entitymodule::C_EquippableItemRuntimeData *>(item.GetRuntimeData());
            if (runtime != nullptr) {
                const auto count = runtime->GetOnEquipBuffCount();
                if (count > KCDC::Shared::RPC::kMaximumOnEquipBuffs) {
                    error = "Native item effect set exceeds the protocol bound";
                    return false;
                }
                for (std::size_t index = 0; index < count; ++index) {
                    SDK::CryGUID guid {};
                    if (!runtime->GetOnEquipBuffAt(index, guid) || (guid.hipart == 0 && guid.lopart == 0)) {
                        error = "Native item effect set is invalid";
                        return false;
                    }
                    captured.push_back({guid.hipart, guid.lopart});
                }
                std::sort(captured.begin(), captured.end());
                if (std::adjacent_find(captured.begin(), captured.end()) != captured.end()) {
                    error = "Native item effect set contains duplicate identities";
                    return false;
                }
            }
        }
        effects = std::move(captured);
        error.clear();
        return true;
    }

    bool ApplyNativeItemEffects(SDK::wh::entitymodule::C_Item &item, const KCDC::Shared::RPC::InventoryItem &properties, std::string &error) {
        const auto itemClass = KCDC::Features::Items::CustomItemModule::Get().ToLogical(item.InvokeGetItemClassRaw());
        if (itemClass.low != properties.itemClass.low || itemClass.high != properties.itemClass.high || !KCDC::Shared::Inventory::AreSupportedOnEquipBuffs(properties)) {
            error = "Unsupported authoritative item effect set";
            return false;
        }
        std::vector<KCDC::Shared::RPC::ItemClassId> current;
        if (!ReadNativeItemEffects(item, current, error)) {
            return false;
        }
        if (current == properties.onEquipBuffs) {
            return true;
        }
        auto *runtime = static_cast<SDK::wh::entitymodule::C_EquippableItemRuntimeData *>(item.GetRuntimeData());
        if (runtime == nullptr && !properties.onEquipBuffs.empty()) {
            runtime = static_cast<SDK::wh::entitymodule::C_EquippableItemRuntimeData *>(item.EnsureRuntimeData());
        }
        if (runtime == nullptr) {
            error = "Native equippable runtime is unavailable";
            return false;
        }
        // Native mutation methods update active equipped contributions and
        // issue the item's own change notification. Never edit the tree.
        for (const auto &buff : current) {
            if (!std::binary_search(properties.onEquipBuffs.begin(), properties.onEquipBuffs.end(), buff)) {
                runtime->RemoveOnEquipBuff({buff.low, buff.high});
            }
        }
        for (const auto &buff : properties.onEquipBuffs) {
            if (!std::binary_search(current.begin(), current.end(), buff)) {
                runtime->AddOnEquipBuff({buff.low, buff.high});
            }
        }
        if (!ReadNativeItemEffects(item, current, error)) {
            return false;
        }
        if (current != properties.onEquipBuffs) {
            error = "Native item effects differ from the committed set";
            return false;
        }
        return true;
    }
} // namespace KCDC::Features::Items
