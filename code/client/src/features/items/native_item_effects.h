#pragma once

#include <utils/safe_win32.h>

#include "sdk/wh/entitymodule/c_item.h"
#include "shared/item_stack.h"

#include <string>
#include <vector>

namespace KCDC::Features::Items {
    /** Reads the entire borrowed native on-equip definition set. */
    bool ReadNativeItemEffects(const SDK::wh::entitymodule::C_Item &item, std::vector<KCDC::Shared::RPC::ItemClassId> &effects, std::string &error);
    /** Applies the exact authoritative set through native add/remove methods. */
    bool ApplyNativeItemEffects(SDK::wh::entitymodule::C_Item &item, const KCDC::Shared::RPC::InventoryItem &properties, std::string &error);
} // namespace KCDC::Features::Items
