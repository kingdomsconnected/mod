#include "inventory_property_policy.h"
#include "game/custom_item_registry.h"

#include "game/inventory_property_classes.h"
#include "game/item_classes.h"
#include "game/missile_catalog.h"
#include "inventory_item_properties.h"

#include <algorithm>
#include <cmath>

namespace KCDC::Shared::Inventory {
    namespace {
        // C_RPGItemHealth::CalculateMinimumHealth and RPG constructor;
        // native evidence and table generation: docs/inventory-properties.md.
        constexpr float kItemHealthOverlapCoefficient = 0.25F;
        constexpr float kUnbreakableMinimumHealth     = 0.01F;
        constexpr float kPropertyFloatTolerance       = 0.000002F;

        float MinimumHealth(const KCDC::Shared::Game::InventoryPropertyClass &item, std::uint32_t quality) {
            if (!item.equippable) {
                return 0.0F;
            }
            if (quality == 1) {
                return item.breakableWeapon ? 0.0F : kUnbreakableMinimumHealth;
            }
            return (static_cast<float>(quality * (quality - 1)) / static_cast<float>(item.maximumQuality * (item.maximumQuality + 1))) * (1.0F - kItemHealthOverlapCoefficient);
        }

        float MaximumHealth(const KCDC::Shared::Game::InventoryPropertyClass &item, std::uint32_t quality) {
            return item.equippable ? static_cast<float>(quality * (quality + 1)) / static_cast<float>(item.maximumQuality * (item.maximumQuality + 1)) : 1.0F;
        }

        // rpg_param `ItemHealthPriceStatusWeight`, registered at 0x180CEC7C3
        // to RPG params offset 0x9C8. No shipped override table touches it.
        constexpr float kItemHealthPriceStatusWeight = 0.99F;

        // CalculateHealthPriceFactor (0x1804697AC): the weighted health in
        // hundredths, rounded half away from zero, in the native float order.
        float HealthPriceFactor(float health) {
            float scaled = (health * kItemHealthPriceStatusWeight + (1.0F - kItemHealthPriceStatusWeight)) * 100.0F;
            scaled       = scaled < 0.0F ? scaled - 0.5F : scaled + 0.5F;
            return static_cast<float>(static_cast<std::int32_t>(scaled)) * 0.0099999998F;
        }

        // GetCurrentUnitPrice and GetNewUnitPrice differ only in the health
        // they pass: the price is a tenth of the authored one scaled by the
        // factor, then ten times that truncated to money units.
        std::uint32_t UnitPrice(std::uint32_t basePrice, float health) {
            const float price = HealthPriceFactor(health) * (static_cast<float>(static_cast<std::int32_t>(basePrice)) * 0.1F);
            return price == 0.0F ? 0U : static_cast<std::uint32_t>(static_cast<std::int32_t>(price * 10.0F));
        }

        float HealthFromCondition(float minimum, float maximum, float condition) {
            // Preserve native Remap_180466E88 arithmetic order, including
            // rounding. condition is already validated in [0,1].
            return maximum - ((1.0F - condition) * (maximum - minimum));
        }
    } // namespace

    bool AreSupportedOnEquipBuffs(const KCDC::Shared::RPC::InventoryItem &item) {
        if (item.onEquipBuffs.empty()) {
            return true;
        }
        if (item.onEquipBuffs.size() != 1) {
            return false;
        }
        const auto *missile = KCDC::Shared::Game::FindMissileClass(item.itemClass);
        // Native reward181FD4559 adds this buff only to category3 (Ammo).
        // The missile catalog resolves aliases to their inherited ammo class.
        if (missile != nullptr && !missile->weapon) {
            return item.onEquipBuffs.front() == KCDC::Shared::Game::ItemClassIdFromGuid(kExperimentalPowderItemBuff);
        }
        // RPG slot 74 0x182CF3F24 gives it to any forged product of item class 27, a weapon. A
        // script's or a mod's recipe may forge any melee weapon, so the class decides, not the
        // shipped recipes' products.
        const auto *entry = KCDC::Shared::Game::FindItemClass(item.itemClass);
        return entry != nullptr && entry->category == "MeleeWeapon" && item.onEquipBuffs.front() == KCDC::Shared::Game::ItemClassIdFromGuid(kHardenedSteelItemBuff);
    }

    bool NormalizeAuthoritativeItemProperties(std::string_view itemClass, const nlohmann::json &input, nlohmann::json &output, std::string &error) {
        const auto *item = KCDC::Shared::Game::FindInventoryPropertyClass(itemClass);
        if (item == nullptr) {
            error = "unknownItemClass";
            return false;
        }
        // The game creates no quest item through C_Inventory::CreateItems
        // (0x182A1EA34); a row of one could never be shown, and the owner's
        // whole projection would fail on it.
        if (item->questItem) {
            error = "questItem";
            return false;
        }
        if (input.contains("customStackKey") || (input.contains("custom") && KCDC::Shared::Game::CustomItemRegistry::Get().Find(itemClass) == nullptr)) {
            error = "invalidMetadata";
            return false;
        }
        KCDC::Shared::RPC::InventoryItem properties;
        properties.inventoryItemId = 1;
        properties.itemClass       = KCDC::Shared::Game::ItemClassIdFromGuid(itemClass);
        properties.amount          = 1;
        if (!ApplyItemProperties(input, properties)) {
            error = "invalidMetadata";
            return false;
        }
        if ((properties.health < 0.0F && properties.health != -1.0F) || (properties.condition < 0.0F && properties.condition != -1.0F)) {
            error = "invalidCreationSentinel";
            return false;
        }
        const bool hasHealth    = properties.health >= 0.0F;
        const bool hasCondition = properties.condition >= 0.0F;
        properties.quality      = properties.quality == 0 ? 1 : properties.quality;
        if (properties.quality > item->maximumQuality || (!item->canHealthChange && properties.quality != 1)) {
            error = "invalidQuality";
            return false;
        }
        if (!item->canHealthChange) {
            if ((hasHealth && properties.health != 1.0F) || (hasCondition && properties.condition != 1.0F)) {
                error = "immutableItemHealth";
                return false;
            }
            properties.health = properties.condition = 1.0F;
        }
        else {
            const float minimum = MinimumHealth(*item, properties.quality);
            const float maximum = MaximumHealth(*item, properties.quality);
            if (hasHealth && (properties.health < minimum - kPropertyFloatTolerance || properties.health > maximum + kPropertyFloatTolerance)) {
                error = "invalidItemHealth";
                return false;
            }
            if (hasCondition) {
                const float health = HealthFromCondition(minimum, maximum, properties.condition);
                if (hasHealth && std::fabs(properties.health - health) > kPropertyFloatTolerance) {
                    error = "contradictoryItemHealth";
                    return false;
                }
                properties.health = health;
            }
            else if (hasHealth) {
                const float health   = std::clamp(properties.health, minimum, maximum);
                properties.condition = std::clamp(1.0F - ((maximum - health) / (maximum - minimum)), 0.0F, 1.0F);
                properties.health    = HealthFromCondition(minimum, maximum, properties.condition);
            }
            else {
                properties.condition = 1.0F;
                properties.health    = maximum;
            }
        }
        // The native runtime stores a weapon-buff definition, not an
        // arbitrary GUID. Reuse the compiled missile poison/charge catalog.
        if (!KCDC::Shared::Game::IsValidMissileGroundItem(properties)) {
            error = "unsupportedPoisonProperties";
            return false;
        }
        if (!AreSupportedOnEquipBuffs(properties)) {
            error = "unsupportedOnEquipBuffs";
            return false;
        }
        // Collapse negative zero in JSON. Phase -1 remains the existing
        // native default sentinel; phase transitions need their own policy.
        if (properties.health == 0.0F) {
            properties.health = 0.0F;
        }
        if (properties.condition == 0.0F) {
            properties.condition = 0.0F;
        }
        output = EncodeItemProperties(properties);
        error.clear();
        return true;
    }

    bool CalculateItemUnitPrices(std::string_view itemClass, const nlohmann::json &input, ItemUnitPrices &output, std::string &error) {
        const auto *entry = KCDC::Shared::Game::FindItemClass(itemClass);
        if (entry == nullptr) {
            error = "invalidItem";
            return false;
        }
        nlohmann::json normalized;
        if (!NormalizeAuthoritativeItemProperties(itemClass, input, normalized, error)) {
            return false;
        }
        // The normalization has just resolved it.
        const auto *properties = KCDC::Shared::Game::FindInventoryPropertyClass(itemClass);
        const auto quality     = normalized.at("quality").get<std::uint32_t>();
        output.pristine        = UnitPrice(entry->price, MaximumHealth(*properties, quality));
        output.current         = UnitPrice(entry->price, normalized.at("health").get<float>());
        return true;
    }
} // namespace KCDC::Shared::Inventory
