#include "shared/game/inventory_property_classes.h"
#include "shared/game/item_classes.h"
#include "shared/game/missile_catalog.h"
#include "shared/inventory_item_properties.h"
#include "shared/inventory_property_policy.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
    constexpr const char *kSword  = "ce1953df-9f10-4138-8fb1-393a11474553";
    constexpr const char *kSage   = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    constexpr const char *kShield = "000a72ec-f904-4e06-8c57-2eac8ab6ec73";
    constexpr const char *kArrow  = "dfea5d01-b25c-414a-9ab4-6911a5f82118";
    // BascinetOpen01_m02_C2
    constexpr const char *kBascinet = "dfefcdf9-ee88-4ce1-be9e-d0a83fa981e7";

    void RequirePropertyPolicy(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    nlohmann::json Normalize(const char *item, const nlohmann::json &metadata) {
        nlohmann::json result;
        std::string error;
        RequirePropertyPolicy(KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(item, metadata, result, error), "valid native property fixture normalizes");
        return result;
    }

    bool Near(float a, float b) {
        return std::fabs(a - b) < 0.000001F;
    }
} // namespace

void InventoryPropertyPolicyTests() {
    // Fixtures follow native item-health formulas, independently of the
    // random CreateItems defaults which must never enter the server ledger.
    const auto sword = Normalize(kSword, nlohmann::json::object());
    RequirePropertyPolicy(sword.at("quality") == 1 && sword.at("condition") == 1 && Near(sword.at("health"), 0.1F), "fresh quality-one sword has full condition but absolute health0.1");
    const auto worn = Normalize(kSword, {{"quality", 2}, {"condition", 0.5}});
    RequirePropertyPolicy(Near(worn.at("health"), 0.1875F), "quality-two sword halfcondition maps overlapping interval0.075..0.3");
    RequirePropertyPolicy(Near(Normalize(kSword, {{"quality", 3}, {"condition", 0}}).at("health"), 0.225F), "quality-three minimum uses native0.25 overlap");
    RequirePropertyPolicy(Near(Normalize(kSword, {{"quality", 4}, {"condition", 1}}).at("health"), 1.0F), "highest-quality fullcondition reaches absolutehealth1");
    RequirePropertyPolicy(Near(Normalize(kSword, {{"quality", 1}, {"condition", 0}}).at("health"), 0.01F), "unbreakable weapon retains native minimumhealth");
    RequirePropertyPolicy(Normalize(kShield, {{"quality", 1}, {"condition", 0}}).at("health") == 0, "breakable shield can reach absolutezero");
    RequirePropertyPolicy(Normalize(kSage, {{"health", 0.375}}).at("condition") == 0.375, "mutable herb health is its condition");
    RequirePropertyPolicy(Normalize(kArrow, nlohmann::json::object()).at("health") == 1, "immutable ammunition always has full health");
    const auto powder = Normalize(kArrow, {{"onEquipBuffs", {std::string(KCDC::Shared::Inventory::kExperimentalPowderItemBuff)}}});
    RequirePropertyPolicy(powder.at("onEquipBuffs").at(0) == std::string(KCDC::Shared::Inventory::kExperimentalPowderItemBuff) && Normalize(kArrow, powder) == powder, "Experimental Powder output keeps its independent persistent buff and normalizes idempotently");
    RequirePropertyPolicy(!Normalize(kArrow, nlohmann::json::object()).contains("onEquipBuffs") && Normalize(kArrow, {{"onEquipBuffs", nlohmann::json::array()}}) == Normalize(kArrow, nlohmann::json::object()),
        "empty item effect sets preserve the historical metadata encoding and receipt identity");
    const auto healthOnly = Normalize(kSword, {{"quality", 2}, {"health", 0.1875}});
    // Native float32 inverse mapping produces 0.4999999403953552 here,
    // although explicitly requested 0.5 maps to the same absolute health.
    // Preserve the requested float condition; compare its native inverse
    // within rounding tolerance, rather than demanding identical JSON.
    RequirePropertyPolicy(healthOnly.at("quality") == worn.at("quality") && healthOnly.at("health") == worn.at("health") && Near(healthOnly.at("condition"), worn.at("condition")), "health-only request resolves native-equivalent condition without randomized constructor branch");
    RequirePropertyPolicy(Normalize(kSword, healthOnly) == healthOnly && Normalize(kSword, worn) == worn, "both explicit-condition and inverse-derived metadata remain exactly idempotent");
    const auto &all = KCDC::Shared::Game::g_InventoryPropertyClasses;
    RequirePropertyPolicy(all.size() == KCDC::Shared::Game::g_ItemClasses.size(), "every shipped item has compiled authoritative class facts");
    std::size_t questItems = 0;
    for (const auto &entry : KCDC::Shared::Game::g_ItemClasses) {
        nlohmann::json first, second;
        std::string error;
        if (KCDC::Shared::Game::FindInventoryPropertyClass(entry.guid)->questItem) {
            ++questItems;
            RequirePropertyPolicy(!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(entry.guid, nlohmann::json::object(), first, error) && error == "questItem", "a quest item is never granted: the game cannot create it");
            continue;
        }
        RequirePropertyPolicy(KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(entry.guid, nlohmann::json::object(), first, error), "every shipped non-quest class supports concrete fresh grants");
        RequirePropertyPolicy(first.at("quality") == 1 && first.at("health").get<float>() >= 0 && first.at("condition") == 1, "no shipped fresh grant retains random creation sentinels");
        RequirePropertyPolicy(KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(entry.guid, first, second, error) && first == second, "persisted canonical defaults remain exactly idempotent");
    }
    RequirePropertyPolicy(questItems == 293, "every shipped IsQuestItem class is compiled as a quest item");
    std::string error;
    nlohmann::json output = {{"untouched", true}};
    for (const auto &invalidBuffs : {nlohmann::json(nullptr), nlohmann::json(true), nlohmann::json("11fd0a23-9871-48e1-8eeb-e5dfac149f9c"), nlohmann::json::array({1}), nlohmann::json::array({"not-a-guid"}), nlohmann::json::array({"00000000-0000-0000-0000-000000000000"}),
             nlohmann::json::array({"11fd0a23-9871-48e1-8eeb-e5dfac149f9c", "11FD0A23-9871-48E1-8EEB-E5DFAC149F9C"}), nlohmann::json::array({"00000000-0000-0000-0000-000000000001"})}) {
        RequirePropertyPolicy(!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(kArrow, {{"onEquipBuffs", invalidBuffs}}, output, error) && output == nlohmann::json({{"untouched", true}}),
            "malformed, duplicate, unknown or nonarray on-equip buffs refuse without partial output");
    }
    RequirePropertyPolicy(!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(kSword, {{"onEquipBuffs", powder.at("onEquipBuffs")}}, output, error) && error == "unsupportedOnEquipBuffs", "Experimental Powder cannot be attached to a non-ammunition item");
    RequirePropertyPolicy(Normalize(kArrow, {{"onEquipBuffs", {"11FD0A23-9871-48E1-8EEB-E5DFAC149F9C"}}}) == powder, "case variants encode one canonical buff identity");
    for (const auto &invalid : {nlohmann::json {{"quality", 5}}, nlohmann::json {{"quality", 1}, {"health", 1}}, nlohmann::json {{"quality", 2}, {"condition", 0.5}, {"health", 0.25}}, nlohmann::json {{"condition", 1.01}}, nlohmann::json {{"condition", -0.5}},
             nlohmann::json {{"health", -0.5}}, nlohmann::json {{"condition", true}}, nlohmann::json {{"quality", -1}}, nlohmann::json {{"health", std::numeric_limits<double>::infinity()}}, nlohmann::json {{"invented", 1}}}) {
        const bool accepted = KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(kSword, invalid, output, error);
        if (accepted || output != nlohmann::json({{"untouched", true}})) {
            throw std::runtime_error("Invalid property fixture: input=" + invalid.dump() + ", accepted=" + std::to_string(accepted) + ", error=" + error + ", output=" + output.dump());
        }
    }
    RequirePropertyPolicy(!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(kArrow, {{"health", 0.5}}, output, error), "immutable ammunition cannot carry engine-discarded wear");
    RequirePropertyPolicy(!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties("00000000-0000-0000-0000-000000000001", {}, output, error), "unknown class cannot infer defaults");

    const auto &poison           = KCDC::Shared::Game::g_MissilePoisons.front();
    nlohmann::json poisonedArrow = {{"poison", KCDC::Shared::Game::CryGuidToString(poison.buff.low, poison.buff.high)}, {"poisonCharges", 1}};
    RequirePropertyPolicy(Normalize(kArrow, poisonedArrow).at("poison") == poisonedArrow.at("poison"), "known native poison definition survives ammunition normalization");
    auto powderAndPoison            = poisonedArrow;
    powderAndPoison["onEquipBuffs"] = powder.at("onEquipBuffs");
    const auto combined             = Normalize(kArrow, powderAndPoison);
    RequirePropertyPolicy(combined.at("onEquipBuffs") == powder.at("onEquipBuffs") && combined.at("poison") == poisonedArrow.at("poison"), "persistent item buff and per-use poison remain independent properties");
    poisonedArrow["poisonCharges"] = 2;
    RequirePropertyPolicy(!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(kArrow, poisonedArrow, output, error) && error == "unsupportedPoisonProperties" && output == nlohmann::json({{"untouched", true}}),
        "ammunition cannot exceed its native one-charge poison capacity");
    poisonedArrow["poisonCharges"] = 1;
    RequirePropertyPolicy(!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(kSage, poisonedArrow, output, error) && error == "unsupportedPoisonProperties", "non-missile class cannot acquire an unsupported native poison runtime");
    poisonedArrow["poison"] = "00000000-0000-0000-0000-000000000002";
    RequirePropertyPolicy(!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(kArrow, poisonedArrow, output, error) && error == "unsupportedPoisonProperties", "invented poison definition is refused before an authoritative row can be committed");

    KCDC::Shared::RPC::InventoryItem parsed;
    parsed.inventoryItemId = 41;
    parsed.itemClass       = {2, 3};
    parsed.amount          = 7;
    parsed.quality         = 2;
    parsed.health          = 0.1875F;
    parsed.condition       = 0.5F;
    parsed.ownerId         = 9007199254740993ULL;
    parsed.equipped        = true;
    parsed.equipmentSlot   = 6;
    const auto unchanged   = parsed;
    for (const auto &invalidValue : {nlohmann::json(true), nlohmann::json(false), nlohmann::json("0.5"), nlohmann::json(nullptr), nlohmann::json::array(), nlohmann::json::object()}) {
        for (const char *key : {"health", "condition"}) {
            const nlohmann::json invalid = {{"quality", 4}, {key, invalidValue}};
            if (KCDC::Shared::Inventory::ApplyItemProperties(invalid, parsed) || parsed != unchanged) {
                throw std::runtime_error("Metadata parser accepted or partially applied nonnumeric property: " + invalid.dump());
            }
        }
    }
    for (const auto &invalid : {nlohmann::json {{"quality", 4}, {"health", std::numeric_limits<double>::infinity()}}, nlohmann::json {{"quality", 4}, {"health", 2}}, nlohmann::json {{"quality", 4}, {"poisonCharges", 1}}}) {
        if (KCDC::Shared::Inventory::ApplyItemProperties(invalid, parsed) || parsed != unchanged) {
            throw std::runtime_error("Metadata parser partially changed destination before final validation: " + invalid.dump());
        }
    }
    RequirePropertyPolicy(KCDC::Shared::Inventory::ApplyItemProperties({{"quality", 1}, {"health", 1}, {"condition", 1.0}}, parsed) && parsed.health == 1.0F && parsed.condition == 1.0F && parsed.inventoryItemId == unchanged.inventoryItemId
                              && parsed.itemClass == unchanged.itemClass && parsed.amount == unchanged.amount,
        "numeric integer/float health succeeds while preserving item identity and quantity");

    for (const char *key : {"ownerId", "stolenFromId", "ownershipFlags", "phase", "poisonBuff"}) {
        RequirePropertyPolicy(!KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(kSword, {{key, 0}}, output, error) && error == "invalidMetadata", "provenance, phase and the old poison spelling are not inventory properties");
    }
    RequirePropertyPolicy(!Normalize(kArrow, nlohmann::json::object()).contains("poison") && !Normalize(kArrow, nlohmann::json::object()).contains("poisonCharges"), "an unpoisoned item encodes no poison keys, so equal items encode equal");

    // Expected values worked through the native float32 sequence separately:
    // the bascinet's Price is 16438 and its quality-two health runs
    // 0.075..0.3, so half condition is 0.1875.
    KCDC::Shared::Inventory::ItemUnitPrices prices;
    RequirePropertyPolicy(KCDC::Shared::Inventory::CalculateItemUnitPrices(kBascinet, {{"quality", 2}, {"condition", 0.5}}, prices, error) && prices.pristine == 5095 && prices.current == 3287, "a worn helmet prices at its own health and pristine at its quality's maximum");
    RequirePropertyPolicy(KCDC::Shared::Inventory::CalculateItemUnitPrices(kBascinet, {{"quality", 4}}, prices, error) && prices.pristine == 16438 && prices.current == 16438, "a fresh top-quality item costs its authored Price");
    RequirePropertyPolicy(KCDC::Shared::Inventory::CalculateItemUnitPrices(kArrow, nlohmann::json::object(), prices, error) && prices.pristine == 10 && prices.current == 10, "ammunition, whose health never changes, costs its authored Price");
    const KCDC::Shared::Inventory::ItemUnitPrices before = prices;
    RequirePropertyPolicy(!KCDC::Shared::Inventory::CalculateItemUnitPrices(kBascinet, {{"quality", 5}}, prices, error) && error == "invalidQuality" && prices.pristine == before.pristine && prices.current == before.current, "a refused property set prices nothing");
    RequirePropertyPolicy(!KCDC::Shared::Inventory::CalculateItemUnitPrices("not-a-guid", nlohmann::json::object(), prices, error) && error == "invalidItem", "an unknown class has no price");
}
