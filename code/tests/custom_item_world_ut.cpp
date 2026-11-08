#include "features/ground_item/ground_stock.h"
#include "features/stash/stash_contents.h"
#include "shared/features/ground_item/ground_item_entity.h"
#include "shared/features/vendor/vendor_rpc.h"
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
} // namespace

void CustomItemWorldTests() {
    auto &registry = KCDC::Shared::Game::CustomItemRegistry::Get();
    registry.Reset();
    const auto type = registry.Register("test:parcel", {{"name", "Parcel"}, {"visual", "bsmt_woodExotic"}, {"weight", 1}}).guid;
    KCDC::Features::Inventory::InventoryIds ids;
    KCDC::Features::Inventory::Inventory player(ids), recipient(ids);
    const auto granted = player.Add(type, 5, {{"custom", {{"name", "Anna's parcel"}, {"data", {{"secret", "server-only-mail-token"}}}}}}, std::nullopt);
    Require(granted.ok, "custom world fixture grant succeeds");
    const auto id       = granted.outputs[0].id;
    const auto metadata = player.Rows().at(id).metadata;
    KCDC::Features::Stash::StashContents contents(ids);
    const KCDC::Features::Stash::StashIdentity chest {700, 700, 0xABCDEFULL, 0};
    auto &stash = contents.Create(chest.contentsId);
    Require(contents.Open(chest, 51, 1000), "custom stash opens");
    const auto deposited = contents.Transfer(chest, 51, player, {chest.networkId, true, {{id, 2}}, player.Revision(), stash.Revision()});
    Require(deposited.ok && stash.Rows().at(deposited.outputs[0].id).metadata == metadata, "deposit preserves custom properties");
    const auto snapshot = contents.Snapshot(chest.networkId, stash, "");
    Require(snapshot.dump().find("server-only-mail-token") == std::string::npos && snapshot.at("rows")[0].at("metadata").contains("customStackKey"), "stash snapshot strips private data and includes equality identity");
    Require(contents.Transfer(chest, 51, player, {chest.networkId, false, {{deposited.outputs[0].id, 2}}, player.Revision(), stash.Revision()}).ok && player.Rows().at(id).amount == 5, "withdrawal merges exact custom properties");
    const auto drop = KCDC::Features::GroundItem::GroundStock::Drop(player, {{id, 2}});
    Require(drop.result.ok && drop.stacks.size() == 1 && drop.stacks[0].item.custom.at("data").at("secret") == "server-only-mail-token", "authoritative ground item retains private data");
    auto item = drop.stacks[0].item;
    MafiaNet::BitStream wire;
    Require(item.Serialize(&wire, true), "custom stack serializes");
    const std::string bytes(reinterpret_cast<const char *>(wire.GetData()), wire.GetNumberOfBytesUsed());
    Require(bytes.find("server-only-mail-token") == std::string::npos, "private data is absent from binary inventory payload");
    KCDC::Shared::RPC::InventoryItem copy;
    Require(copy.Serialize(&wire, false) && copy.custom.at("name") == "Anna's parcel" && !copy.custom.contains("data") && copy.customStackKey == item.customStackKey, "wire roundtrip preserves display and opaque identity only");
    KCDC::Shared::Entities::GroundItemEntity ground, observer;
    ground.item = item;
    MafiaNet::BitStream construction;
    Framework::Networking::Replication::FieldSerializer writer(&construction, true), reader(&construction, false);
    ground.OnSerializeConstruction(writer);
    observer.OnSerializeConstruction(reader);
    Require(reader.Good() && observer.item.custom == copy.custom && observer.item.customStackKey == copy.customStackKey, "ground construction uses the same private-data boundary");
    const auto picked = KCDC::Features::GroundItem::GroundStock::Pickup(recipient, item);
    Require(picked.ok && recipient.Rows().at(picked.outputs[0].id).metadata == metadata, "pickup restores private properties from server state");
    Require(recipient.TakeNotices().at(type) == 2, "normal custom pickup announces the acquired units");
    Require(KCDC::Features::GroundItem::GroundStock::Pickup(recipient, item, "dropRefund", false).ok && recipient.TakeNotices().empty(), "custom drop refunds honor explicit notice suppression");
    const auto notified = KCDC::Features::GroundItem::GroundStock::Pickup(recipient, item, "pickup", true);
    Require(notified.ok && recipient.Rows().at(picked.outputs[0].id).amount == 6 && recipient.TakeNotices().at(type) == 2, "custom pickup strips public token, merges metadata and honors explicit notices");
    KCDC::Shared::RPC::VendorStockRow vendor {item.itemClass, item.amount, 10, item};
    MafiaNet::BitStream shelf;
    Require(vendor.Serialize(&shelf, true), "vendor properties serialize");
    KCDC::Shared::RPC::VendorStockRow received;
    Require(received.Serialize(&shelf, false) && !received.properties.custom.contains("data") && received.properties.customStackKey == item.customStackKey, "vendor wire strips private data");
    KCDC::Shared::RPC::VendorStock stock;
    stock.sessionId = 7;
    stock.revision  = 3;
    for (const auto &source : {nlohmann::json::object(), metadata, nlohmann::json::object()}) {
        std::string error;
        nlohmann::json normalized;
        Require(KCDC::Shared::Inventory::NormalizeAuthoritativeItemProperties(type, source, normalized, error), "vendor fixture metadata normalizes");
        KCDC::Shared::RPC::VendorStockRow row {item.itemClass, 20, 12};
        row.properties.inventoryItemId = 1;
        row.properties.itemClass       = row.itemClass;
        row.properties.amount          = row.amount;
        Require(KCDC::Shared::Inventory::ApplyItemProperties(registry.PublicMetadata(normalized), row.properties), "vendor public properties apply");
        stock.stock.push_back(row);
    }
    stock.buys.push_back({item.itemClass, 6});
    MafiaNet::BitStream lists;
    stock.Serialize(&lists, true);
    Require(stock.IsSane(), "complete vendor list serializes");
    KCDC::Shared::RPC::VendorStock readStock;
    readStock.Serialize(&lists, false);
    Require(readStock.IsSane() && readStock.stock.size() == 3 && readStock.buys.size() == 1, "vendor list accepts default and personalized rows together");
    Require(readStock.stock[0].properties.custom.empty() && readStock.stock[1].properties.custom.at("name") == "Anna's parcel" && readStock.stock[2].properties.custom.empty(), "vendor list keeps empty and personalized display metadata distinct");
    Require(readStock.stock[0].amount == 20 && readStock.stock[0].price == 12 && readStock.buys[0].price == 6, "empty metadata preserves following rows and buy prices");
    const std::string listBytes(reinterpret_cast<const char *>(lists.GetData()), lists.GetNumberOfBytesUsed());
    Require(listBytes.find("server-only-mail-token") == std::string::npos, "complete vendor list does not reveal private properties");
    MafiaNet::BitStream shortLists(lists.GetData(), lists.GetNumberOfBytesUsed() - 1, true);
    readStock.Serialize(&shortLists, false);
    Require(!readStock.IsSane(), "truncated vendor list is rejected whole");
    KCDC::Shared::RPC::InventoryItem plain;
    plain.inventoryItemId = 1;
    plain.itemClass       = KCDC::Shared::Game::ItemClassIdFromGuid("b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4");
    plain.amount          = 1;
    MafiaNet::BitStream emptyDisplay;
    Require(plain.Serialize(&emptyDisplay, true), "ordinary item with absent display serializes");
    KCDC::Shared::RPC::InventoryItem readPlain;
    Require(readPlain.Serialize(&emptyDisplay, false) && readPlain == plain && emptyDisplay.GetNumberOfUnreadBits() < 8, "ordinary item accepts empty display at the end of a payload");
    MafiaNet::BitStream shortDisplay(emptyDisplay.GetData(), emptyDisplay.GetNumberOfBytesUsed() - 1, true);
    Require(!readPlain.Serialize(&shortDisplay, false), "a truncated empty-display length is still rejected");
    MafiaNet::BitStream truncated(wire.GetData(), wire.GetNumberOfBytesUsed() - 1, true);
    Require(!copy.Serialize(&truncated, false), "truncated custom payload is refused");
    registry.Reset();
}
