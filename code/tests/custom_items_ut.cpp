#include "features/inventory/inventory.h"
#include "features/items/custom_item_presentation.h"
#include "shared/game/custom_item_registry.h"
#include "shared/inventory_item_properties.h"
#include "shared/inventory_property_policy.h"
#include <stdexcept>

namespace {
    void Require(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }
    nlohmann::json Resource() {
        return {{"name", "Wood"}, {"visual", "bsmt_woodExotic"}, {"weight", 2.0}, {"price", 12}};
    }
    nlohmann::json Letter(const char *mail) {
        return {{"custom", {{"name", "Letter from Anna"}, {"description", "Addressed to Henry"}, {"data", {{"mailId", mail}}}}}};
    }
} // namespace

void CustomItemTests() {
    auto &registry = KCDC::Shared::Game::CustomItemRegistry::Get();
    registry.Reset();
    const auto wood      = registry.Register("test:wood", Resource()).guid;
    const auto native    = registry.Find(wood)->nativeGuid;
    const auto logicalId = KCDC::Shared::Game::ItemClassIdFromGuid(wood);
    const auto nativeId  = KCDC::Features::Items::CustomItemPresentation::ToNative(logicalId);
    Require(wood != native && KCDC::Shared::Game::CryGuidToString(nativeId.m_low, nativeId.m_high) == native, "native inventory and receipt icons use the registered class, not the logical ID");
    const auto ordinaryId     = KCDC::Shared::Game::ItemClassIdFromGuid("b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4");
    const auto ordinaryNative = KCDC::Features::Items::CustomItemPresentation::ToNative(ordinaryId);
    Require(ordinaryNative.m_low == ordinaryId.low && ordinaryNative.m_high == ordinaryId.high, "ordinary receipt IDs pass through unchanged");
    for (const auto *field : {"n", "d", "u"}) {
        const auto key = KCDC::Features::Items::CustomItemPresentation::TypeLabel(native, field);
        Require(key == "@" + KCDC::Features::Items::CustomItemPresentation::TypeKey(native, field) && key.find('-') == std::string::npos, "type names, descriptions and actions trigger native localization");
    }
    Require(registry.Register("test:wood", Resource()).guid == wood && registry.Revision() == 1, "identical registration is idempotent");
    auto explicitDefaults = Resource();
    explicitDefaults.update({{"description", ""}, {"stackable", true}, {"usable", false}, {"useLabel", "Use"}});
    Require(registry.Register("test:wood", explicitDefaults).guid == wood, "defaults participate canonically in identity");
    auto conflicting      = Resource();
    conflicting["weight"] = 3;
    bool rejected         = false;
    try {
        registry.Register("test:wood", conflicting);
    }
    catch (const std::invalid_argument &) {
        rejected = true;
    }
    Require(rejected && registry.Find(wood)->weight == 2, "conflicting registration leaves immutable definition intact");
    auto invalid      = Resource();
    invalid["visual"] = "unknown visual";
    rejected          = false;
    try {
        registry.Register("test:invalid", invalid);
    }
    catch (const std::invalid_argument &) {
        rejected = true;
    }
    Require(rejected && registry.Revision() == 1, "unknown appearance is rejected atomically");
    for (const auto price : {2147483584U, 2147483647U}) {
        invalid          = Resource();
        invalid["price"] = price;
        rejected         = false;
        try {
            registry.Register("test:unsafe-price", invalid);
        }
        catch (const std::invalid_argument &) {
            rejected = true;
        }
        Require(rejected && registry.Revision() == 1, "prices that overflow native float-to-int conversion are rejected atomically");
    }
    Require(KCDC::Shared::Game::FindItemClass(wood)->price == 12 && KCDC::Shared::Game::FindInventoryPropertyClass(wood)->nativeStackable, "combined catalogs expose custom pricing and stackability");

    KCDC::Features::Inventory::InventoryIds ids;
    KCDC::Features::Inventory::Inventory inventory(ids), recipient(ids);
    const auto added = inventory.Add(wood, 10, nlohmann::json::object(), std::nullopt);
    Require(added.ok, "custom resource can be granted immediately after registration");
    const auto row = added.outputs.front().id;
    Require(inventory.Add(wood, 2, nlohmann::json::object(), std::nullopt).outputs.front().id == row && inventory.Rows().at(row).amount == 12, "equal resource properties stack");
    Require(inventory.ValidateCustomUse(row, inventory.Revision()) == nullptr, "unusable custom resources do not dispatch actions");

    auto letterDefinition = Resource();
    letterDefinition.update({{"name", "Letter"}, {"visual", "a54ab5ef-d4a2-4929-9045-1a1efde935c5"}, {"stackable", false}, {"usable", true}, {"useLabel", "Read"}});
    const auto letter = registry.Register("test:letter", letterDefinition).guid;
    Require(registry.Revision() == 2 && registry.Find(wood) != nullptr, "late registration retains earlier definitions");
    const auto letters = inventory.Add(letter, 2, Letter("letter-123"), std::nullopt);
    Require(letters.ok && letters.outputs.size() == 2 && letters.outputs[0].id != letters.outputs[1].id && letters.outputs[0].amount == 1, "nonstackable grants create distinct unit rows even with identical data");
    Require(inventory.TakeNotices().empty(), "custom grants are silent unless their caller requests a notice");
    Require(inventory.Add(letter, 2, Letter("letter-notice"), std::nullopt, "add", true).ok && inventory.TakeNotices().at(letter) == 2, "nonstackable grants notify the sum across new rows");
    const auto first = letters.outputs[0].id, second = letters.outputs[1].id;
    const auto properties = inventory.Rows().at(first).metadata;
    const auto revision   = inventory.Revision();
    Require(inventory.ValidateCustomUse(first, revision) != nullptr && recipient.ValidateCustomUse(first, revision) == nullptr, "use validates ownership");
    Require(inventory.ValidateCustomUse(first, revision - 1) == nullptr, "use refuses stale revisions");
    Require(inventory.UpdateCustom(first, {{"name", "A different letter"}, {"data", {{"mailId", "letter-456"}}}}, revision).ok, "instance updates succeed");
    Require(inventory.Rows().at(first).amount == 1 && inventory.Rows().at(first).metadata.at("quality") == properties.at("quality") && inventory.Rows().at(second).metadata == properties, "updates preserve row identity/native properties and isolate other letters");
    Require(inventory.UpdateCustom(first, {{"name", "stale"}}, revision).code == "staleRevision", "stale update changes nothing");
    nlohmann::json tooDeep = nlohmann::json::object();
    for (int depth = 0; depth < 64; ++depth) {
        tooDeep = {{"child", std::move(tooDeep)}};
    }
    Require(inventory.UpdateCustom(first, {{"data", tooDeep}}, std::nullopt).code == "invalidMetadata", "over-deep data is refused before it could produce a null event row");
    Require(inventory.Rows().at(first).metadata.at("custom").at("description") == "Addressed to Henry", "omitted override stays unchanged");
    Require(inventory.UpdateCustom(first, {{"name", nullptr}, {"description", nullptr}, {"data", nullptr}}, std::nullopt).ok && !inventory.Rows().at(first).metadata.contains("custom"), "null resets overrides and clears data");
    const auto moved = KCDC::Features::Inventory::Inventory::Transfer(inventory, recipient, {{second, 1}}, inventory.Revision(), recipient.Revision());
    Require(moved.ok && recipient.Rows().at(moved.outputs[0].id).metadata == properties, "player trade preserves private data");
    const auto bought = recipient.Exchange({}, {{letter, 1, properties}}, recipient.Revision(), "trade");
    Require(bought.ok && recipient.Rows().at(bought.outputs[0].id).metadata == properties, "vendor exchange preserves credited custom properties");
    Require(recipient.ValidateCustomUse(bought.outputs[0].id, recipient.Revision()) != nullptr, "usable letter is dispatchable without consumption");
    const auto count = recipient.Rows().size();
    recipient.ValidateCustomUse(bought.outputs[0].id, recipient.Revision());
    Require(recipient.Rows().size() == count, "use validation does not consume; scripts control consumption");
    Require(recipient.Remove({{bought.outputs[0].id, 1}}, recipient.Revision()).ok, "script can explicitly consume a used letter");

    auto tokenA       = registry.PublicMetadata(properties);
    const auto tokenB = registry.PublicMetadata(Letter("letter-456"));
    Require(!tokenA.at("custom").contains("data") && tokenA.at("customStackKey") == registry.PublicMetadata(properties).at("customStackKey") && tokenA.at("customStackKey") != tokenB.at("customStackKey"), "public properties retain opaque equality but not script data");
    auto variant = inventory.Add(wood, 2, Letter("wood-batch-a"), std::nullopt);
    auto other   = inventory.Add(wood, 2, Letter("wood-batch-b"), std::nullopt);
    Require(variant.ok && other.ok && variant.outputs[0].id != other.outputs[0].id, "different private data keeps stackable rows separate");
    Require(inventory.Add(wood, 1, Letter("wood-batch-a"), std::nullopt).outputs[0].id == variant.outputs[0].id, "matching private data stacks");
    KCDC::Features::Inventory::Inventory limited(ids, 1);
    Require(!limited.Add(letter, 2, properties, std::nullopt).ok && limited.Rows().empty(), "nonstackable capacity failure is atomic");

    KCDC::Features::Inventory::Inventory packed(ids);
    while (packed.Add(letter, 1, {{"custom", {{"name", "A"}}}}, std::nullopt).ok) {}
    std::map<std::string, nlohmann::json> publicRows;
    for (const auto &[id, value] : packed.Rows()) {
        publicRows[id] = {{"item", value.itemClass}, {"amount", value.amount}, {"metadata", registry.PublicMetadata(value.metadata)}};
    }
    Require(nlohmann::json(publicRows).dump().size() < 512 * 1024 - 4096, "inventory capacity reserves the public stacking-token overhead");

    const auto savedRow = recipient.Rows().begin()->second;
    registry.Reset();
    const auto disconnectedNative = KCDC::Features::Items::CustomItemPresentation::ToNative(logicalId);
    Require(disconnectedNative.m_low == logicalId.low && disconnectedNative.m_high == logicalId.high, "session reset drops the previous native receipt mapping");
    KCDC::Features::Inventory::Inventory restored(ids);
    Require(!restored.Set({savedRow}, std::nullopt).ok, "restoration requires definitions first");
    Require(registry.Register("test:letter", letterDefinition).guid == letter && restored.Set({savedRow}, std::nullopt).ok && restored.Rows().at(savedRow.id) == savedRow, "registration then restore preserves row id and private data");
    Require(registry.Register("test:wood", conflicting).guid == wood && registry.Find(wood)->nativeGuid != native, "logical identity survives changed definition on another server; native identity changes");
    const auto reconnectedNative = KCDC::Features::Items::CustomItemPresentation::ToNative(logicalId);
    Require(KCDC::Shared::Game::CryGuidToString(reconnectedNative.m_low, reconnectedNative.m_high) == registry.Find(wood)->nativeGuid, "receipt mapping follows the new definition after reconnect");
    auto maximumPrice     = Resource();
    maximumPrice["price"] = 2147483583U;
    const auto expensive  = registry.Register("test:maximum-price", maximumPrice).guid;
    KCDC::Shared::Inventory::ItemUnitPrices prices;
    std::string error;
    Require(KCDC::Shared::Inventory::CalculateItemUnitPrices(expensive, nlohmann::json::object(), prices, error) && prices.current == 2147483520U && prices.pristine == prices.current, "largest supported price stays positive through native pricing arithmetic");
    registry.Reset();
}
