#pragma once

#include <nlohmann/json.hpp>

#include "item_stack.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace KCDC::Shared::Inventory {
    inline constexpr unsigned kAuthoritativeItemPropertyPolicyVersion = 1;
    inline constexpr std::string_view kExperimentalPowderItemBuff     = "11fd0a23-9871-48e1-8eeb-e5dfac149f9c";
    inline constexpr std::string_view kHardenedSteelItemBuff          = "e82de782-c961-4b15-8bd9-696d4be52058";

    /** Current native-proven item-effect projection support. */
    bool AreSupportedOnEquipBuffs(const KCDC::Shared::RPC::InventoryItem &item);

    /**
     * Resolve creation sentinels to the server's deterministic fresh policy
     * (quality one, full condition), not the game's randomized defaults.
     * Validate class limits and derive consistent float health/condition.
     * Output changes only on success. Existing non-health properties survive.
     */
    bool NormalizeAuthoritativeItemProperties(std::string_view itemClass, const nlohmann::json &input, nlohmann::json &output, std::string &error);

    /** What the game prices one unit at, in money units, before any shop's terms. */
    struct ItemUnitPrices {
        // At the full health its quality allows: C_RPGInventory::GetNewUnitPrice (0x18096F6C0).
        std::uint32_t pristine = 0;
        // At its own health: C_RPGInventory::GetCurrentUnitPrice (0x18064A024).
        std::uint32_t current = 0;
    };

    /**
     * Prices one unit of `itemClass` with these properties, read the way
     * `NormalizeAuthoritativeItemProperties` reads them and refused for the
     * same reasons. Output changes only on success.
     */
    bool CalculateItemUnitPrices(std::string_view itemClass, const nlohmann::json &input, ItemUnitPrices &output, std::string &error);
} // namespace KCDC::Shared::Inventory
