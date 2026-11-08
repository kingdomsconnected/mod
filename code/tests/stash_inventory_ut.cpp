#include "features/stash/stash_contents.h"
#include "shared/features/stash/stash_rpc.h"

#include <limits>
#include <stdexcept>

namespace {
    constexpr const char *kSage  = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    constexpr const char *kArrow = "dfea5d01-b25c-414a-9ab4-6911a5f82118";

    constexpr std::uint64_t kPlayer = 51;
    constexpr std::uint64_t kOther  = 52;

    const KCDC::Features::Stash::StashIdentity kChest {700, 700, 0xABCDEFULL, 0};

    void RequireStash(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    std::vector<std::string> Names(const std::vector<KCDC::Features::Stash::StashEvent> &events) {
        std::vector<std::string> names;
        for (const auto &event : events) {
            names.push_back(event.name);
        }
        return names;
    }

    KCDC::Features::Stash::StashTransfer Move(bool deposit, const std::string &row, std::uint32_t amount, const KCDC::Features::Inventory::Inventory &player, const KCDC::Features::Inventory::Inventory &stash) {
        return {kChest.networkId, deposit, {{row, amount}}, player.Revision(), stash.Revision()};
    }

    void DepositAndWithdraw() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory player(ids);
        KCDC::Features::Stash::StashContents contents(ids);
        auto &stash     = contents.Create(kChest.contentsId);
        const auto sage = player.Add(kSage, 5, nlohmann::json::object(), std::nullopt).outputs[0].id;
        player.TakeChanges();

        RequireStash(contents.Open(kChest, kPlayer, 1000) && contents.HolderOf(kChest.contentsId) == kPlayer, "a free container opens for the player");
        const auto deposited = contents.Transfer(kChest, kPlayer, player, Move(true, sage, 2, player, stash));
        RequireStash(deposited.ok && player.Rows().at(sage).amount == 3 && stash.Rows().size() == 1, "a deposit moves exactly the named units");
        const auto playerChanges = player.TakeChanges();
        RequireStash(playerChanges.size() == 1 && playerChanges[0].reason == "deposit", "the player's side records a deposit");

        const auto stored = stash.Rows().begin()->first;
        RequireStash(contents.Transfer(kChest, kPlayer, player, Move(false, stored, 2, player, stash)).ok && player.Rows().at(sage).amount == 5 && stash.Rows().empty(), "a withdrawal moves the units back");
        RequireStash(player.TakeChanges()[0].reason == "withdraw", "the player's side records a withdrawal");

        const auto events = contents.TakeEvents();
        RequireStash(Names(events) == std::vector<std::string> {"stashOpen", "stashInventoryChanged", "stashInventoryChanged"}, "open, then one change per transfer");
        RequireStash(events[1].reason == "deposit" && events[1].playerId == kPlayer && events[1].change && events[1].change->reason == "deposit" && events[2].reason == "withdraw", "each change names its reason and player");
    }

    void StaleRevisionsAreRefused() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory player(ids);
        KCDC::Features::Stash::StashContents contents(ids);
        auto &stash     = contents.Create(kChest.contentsId);
        const auto sage = player.Add(kSage, 5, nlohmann::json::object(), std::nullopt).outputs[0].id;
        RequireStash(contents.Open(kChest, kPlayer, 1000), "fixture opens");
        auto stale          = Move(true, sage, 1, player, stash);
        stale.stashRevision = 9;
        RequireStash(contents.Transfer(kChest, kPlayer, player, stale).code == "staleRevision", "a stale container revision is refused");
        stale                = Move(true, sage, 1, player, stash);
        stale.playerRevision = 9;
        RequireStash(contents.Transfer(kChest, kPlayer, player, stale).code == "staleRevision" && player.Rows().at(sage).amount == 5 && stash.Rows().empty(), "a stale player revision is refused and nothing moves");
    }

    void ContainersHoldAtMostTheirCap() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory player(ids);
        KCDC::Features::Stash::StashContents contents(ids);
        auto &stash = contents.Create(kChest.contentsId);
        for (std::size_t row = 0; row < KCDC::Shared::RPC::kMaxStashItems; ++row) {
            RequireStash(stash.Add(kSage, 1, {{"health", 0.01 + (static_cast<double>(row) / 1000.0)}}, std::nullopt).ok, "the container fills up to its cap");
        }
        const auto arrows = player.Add(kArrow, 3, nlohmann::json::object(), std::nullopt).outputs[0].id;
        RequireStash(contents.Open(kChest, kPlayer, 1000), "fixture opens");
        RequireStash(contents.Transfer(kChest, kPlayer, player, Move(true, arrows, 1, player, stash)).code == "inventoryCapacity" && player.Rows().at(arrows).amount == 3, "a row past the cap is refused before anything moves");
    }

    void OnlyTheHolderMovesStock() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory player(ids), other(ids);
        KCDC::Features::Stash::StashContents contents(ids);
        auto &stash       = contents.Create(kChest.contentsId);
        const auto arrows = other.Add(kArrow, 3, nlohmann::json::object(), std::nullopt).outputs[0].id;
        RequireStash(contents.Transfer(kChest, kOther, other, Move(true, arrows, 1, other, stash)).code == "notHolder", "nobody moves stock through a closed container");
        RequireStash(contents.Open(kChest, kPlayer, 1000) && !contents.Open(kChest, kOther, 1000), "a held container refuses a second viewer");
        RequireStash(contents.Transfer(kChest, kOther, other, Move(true, arrows, 1, other, stash)).code == "notHolder", "only the holder moves stock");
        RequireStash(!contents.Close(kChest, kOther, "closed") && contents.HolderOf(kChest.contentsId) == kPlayer, "only the holder closes it");
        RequireStash(contents.Close(kChest, kPlayer, "closed") && contents.Open(kChest, kOther, 1000), "a closed container is free again");
        RequireStash(contents.Transfer(kChest, kPlayer, player, Move(false, arrows, 1, player, stash)).code == "notHolder", "the previous holder has no rights left");
    }

    void LinkedChildrenShareTheirMaster() {
        RequireStash(KCDC::Features::Stash::StashContents::ContentsIdOf(701, 700) == 700 && KCDC::Features::Stash::StashContents::ContentsIdOf(700, 0) == 700, "a linked child's stock is its master's");
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Stash::StashContents contents(ids);
        contents.Create(700);
        const KCDC::Features::Stash::StashIdentity child {701, 700, 0x123ULL, 0};
        RequireStash(contents.Open(child, kPlayer, 1000) && !contents.Open(kChest, kOther, 1000), "opening the child holds the master's stock");
        RequireStash(contents.ViewOf(700) != nullptr && contents.ViewOf(700)->networkId == 701, "the lid that opens is the child's own");
    }

    void ScriptChangesAreReported() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Stash::StashContents contents(ids);
        auto &stash = contents.Create(kChest.contentsId);
        RequireStash(stash.Set({{"", kSage, 2, nlohmann::json::object()}}, std::nullopt).ok, "a script fills the container");
        contents.Changed(kChest, 0);
        const auto events = contents.TakeEvents();
        RequireStash(events.size() == 1 && events[0].name == "stashInventoryChanged" && events[0].playerId == 0 && events[0].reason == "set", "a script change is reported with no player");
        contents.Changed(kChest, 0);
        RequireStash(contents.TakeEvents().empty(), "nothing changed, nothing reported");
    }

    void DestroyReportsInOrder() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Stash::StashContents contents(ids);
        auto &stash = contents.Create(kChest.contentsId);
        RequireStash(stash.Add(kSage, 2, nlohmann::json::object(), std::nullopt).ok && contents.Open(kChest, kPlayer, 1000), "fixture holds a full container open");
        contents.Changed(kChest, 0);
        contents.TakeEvents();
        contents.Destroy(kChest);
        const auto events = contents.TakeEvents();
        RequireStash(Names(events) == std::vector<std::string> {"stashClose", "stashInventoryChanged", "stashDestroy"} && events[0].reason == "destroyed" && events[1].reason == "destroy", "close, then the contents, then the container");
        RequireStash(contents.Find(kChest.contentsId) == nullptr && contents.HolderOf(kChest.contentsId) == 0, "a destroyed container is forgotten");

        contents.Create(kChest.contentsId);
        contents.Destroy(kChest);
        RequireStash(Names(contents.TakeEvents()) == std::vector<std::string> {"stashDestroy"}, "an empty, closed container only goes");
    }

    void ViewsEnd() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Stash::StashContents contents(ids);
        contents.Create(kChest.contentsId);
        RequireStash(contents.Open(kChest, kPlayer, 1000), "fixture opens");
        RequireStash(contents.Expire(1000 + KCDC::Features::Stash::StashContents::kViewTimeoutMs - 1).empty(), "a view lasts until its deadline");
        const auto expired = contents.Expire(1000 + KCDC::Features::Stash::StashContents::kViewTimeoutMs);
        RequireStash(expired.size() == 1 && expired[0].networkId == kChest.networkId && contents.HolderOf(kChest.contentsId) == 0, "an abandoned view expires");
        RequireStash(contents.TakeEvents().back().reason == "timeout", "and says so");

        RequireStash(contents.Open(kChest, kPlayer, 5000) && contents.Release(kPlayer).size() == 1 && contents.HolderOf(kChest.contentsId) == 0, "a departing player releases what they held");
        RequireStash(contents.TakeEvents().back().reason == "disconnected", "and says so");

        RequireStash(KCDC::Features::Stash::StashContents::CanAccess(0, 0, true, 2.0F, 5.0F), "a player at the container may use it");
        RequireStash(!KCDC::Features::Stash::StashContents::CanAccess(0, 0, true, 5.5F, 5.0F) && !KCDC::Features::Stash::StashContents::CanAccess(0, 3, true, 1.0F, 5.0F) && !KCDC::Features::Stash::StashContents::CanAccess(0, 0, false, 1.0F, 5.0F),
            "distance, world and incapacity end access");
    }

    void HoldersKeepTheirView() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Stash::StashContents contents(ids);
        contents.Create(kChest.contentsId);
        RequireStash(contents.Open(kChest, kPlayer, 1000), "fixture opens");
        contents.TakeEvents();
        RequireStash(!contents.Renew(kChest.contentsId, kOther, 50000), "only the holder keeps a view alive");
        RequireStash(contents.Renew(kChest.contentsId, kPlayer, 100000), "the holder keeps a view alive");
        RequireStash(contents.Expire(1000 + KCDC::Features::Stash::StashContents::kViewTimeoutMs).empty() && contents.HolderOf(kChest.contentsId) == kPlayer, "a renewed view outlives its first deadline");
        RequireStash(contents.TakeEvents().empty(), "a renewal is not another open");
        RequireStash(contents.Expire(100000 + KCDC::Features::Stash::StashContents::kViewTimeoutMs).size() == 1, "a view nobody renews still expires");
    }

    void VirtualAccessKeepsSessionChecks() {
        RequireStash(KCDC::Features::Stash::StashContents::CanAccess(3, 3, true, 100000.0F, 5.0F, true), "virtual stock has no reach requirement");
        RequireStash(KCDC::Features::Stash::StashContents::CanAccess(3, 3, true, std::numeric_limits<float>::quiet_NaN(), 5.0F, true), "virtual stock does not depend on a position");
        RequireStash(!KCDC::Features::Stash::StashContents::CanAccess(3, 4, true, 0.0F, 5.0F, true), "virtual stock still belongs to its world");
        RequireStash(!KCDC::Features::Stash::StashContents::CanAccess(3, 3, false, 0.0F, 5.0F, true), "an incapacitated player loses virtual access");
        RequireStash(!KCDC::Features::Stash::StashContents::CanAccess(3, 3, true, 100000.0F, 5.0F), "physical stock still requires reach");
    }

    void StockOutlivesVirtualViews() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory player(ids);
        KCDC::Features::Stash::StashContents contents(ids);
        const KCDC::Features::Stash::StashIdentity virtualStash {701, 701, 0, 3};
        auto &stock         = contents.Create(virtualStash.contentsId);
        const auto row      = player.Add(kSage, 5, {{"health", 0.75}}, std::nullopt).outputs[0].id;
        const auto metadata = player.Rows().at(row).metadata;
        RequireStash(contents.Open(virtualStash, kPlayer, 1000), "stock without an entity GUID opens");
        RequireStash(!contents.Open(virtualStash, kOther, 1000), "virtual stock has an exclusive holder");
        RequireStash(contents.Transfer(virtualStash, kPlayer, player, {virtualStash.networkId, true, {{row, 3}}, player.Revision(), stock.Revision()}).ok, "the holder deposits units");
        const auto stored = stock.Rows().begin()->first;
        RequireStash(contents.Close(virtualStash, kPlayer, "closed"), "the view closes");
        RequireStash(contents.Transfer(virtualStash, kPlayer, player, {virtualStash.networkId, false, {{stored, 1}}, player.Revision(), stock.Revision()}).code == "notHolder", "a closed view moves nothing");
        RequireStash(contents.Open(virtualStash, kPlayer, 2000) && stock.Rows().at(stored).amount == 3 && stock.Rows().at(stored).metadata == metadata, "reopening preserves units and metadata");
        RequireStash(contents.Transfer(virtualStash, kPlayer, player, {virtualStash.networkId, false, {{stored, 3}}, player.Revision(), stock.Revision()}).ok && player.Rows().at(row).amount == 5 && stock.Rows().empty(), "the holder withdraws what was stored");
    }

    void OnlyAnOpenRevealsContents() {
        RequireStash(KCDC::Features::Stash::StashContents::RevealsContents(KCDC::Shared::RPC::StashAction::Open), "an open grant shows what is inside");
        RequireStash(!KCDC::Features::Stash::StashContents::RevealsContents(KCDC::Shared::RPC::StashAction::Unlock) && !KCDC::Features::Stash::StashContents::RevealsContents(KCDC::Shared::RPC::StashAction::Lockpick), "a key or a pick shows nothing before the lock is beaten");
    }

    void ContentsAreKnownWithoutTheRows() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Stash::StashContents contents(ids);
        auto &stash = contents.Create(kChest.contentsId);
        RequireStash(!contents.HasContents(kChest.contentsId) && !contents.HasContents(999), "an empty or unknown container holds nothing");
        RequireStash(stash.Add(kSage, 1, nlohmann::json::object(), std::nullopt).ok && contents.HasContents(kChest.contentsId), "a filled container says so");
    }

    void DestroyKeepsUnreportedChanges() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Stash::StashContents contents(ids);
        auto &stash = contents.Create(kChest.contentsId);
        RequireStash(stash.Add(kSage, 2, nlohmann::json::object(), std::nullopt).ok, "a change nobody reported yet");
        contents.Destroy(kChest);
        const auto events = contents.TakeEvents();
        RequireStash(Names(events) == std::vector<std::string> {"stashInventoryChanged", "stashInventoryChanged", "stashDestroy"} && events[0].reason == "add" && events[1].reason == "destroy", "destroy reports what was pending before it empties the container");
    }

    void DropsDepositWithoutAHolder() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory player(ids);
        KCDC::Features::Stash::StashContents contents(ids);
        auto &stash       = contents.Create(kChest.contentsId);
        const auto sage   = player.Add(kSage, 5, nlohmann::json::object(), std::nullopt).outputs[0].id;
        const auto arrows = player.Add(kArrow, 3, nlohmann::json::object(), std::nullopt).outputs[0].id;
        player.TakeChanges();

        const auto dropped = contents.Deposit(kChest, kPlayer, player, {{sage, 5}, {arrows, 3}});
        RequireStash(dropped.ok && player.Rows().empty() && stash.Rows().size() == 2 && contents.HolderOf(kChest.contentsId) == 0, "a drop moves everything named into the container, with nobody holding it");
        RequireStash(player.TakeChanges()[0].reason == "drop", "the player's side records a drop");
        const auto events = contents.TakeEvents();
        RequireStash(Names(events) == std::vector<std::string> {"stashInventoryChanged"} && events[0].reason == "drop" && events[0].playerId == kPlayer, "the container reports the drop");
        RequireStash(!contents.Deposit({999, 999, 0, 0}, kPlayer, player, {{sage, 1}}).ok, "an unknown container takes nothing");
    }

    void TransfersAreRead() {
        KCDC::Features::Stash::StashTransfer transfer;
        RequireStash(KCDC::Features::Stash::StashContents::ReadTransfer(R"({"stash":700,"deposit":true,"units":[{"id":"12","amount":1}],"playerRevision":57,"stashRevision":9})", transfer) && transfer.stash == 700 && transfer.deposit && transfer.units.size() == 1
                         && transfer.playerRevision == 57 && transfer.stashRevision == 9,
            "a transfer names everything it moves");
        RequireStash(!KCDC::Features::Stash::StashContents::ReadTransfer(R"({"stash":700,"deposit":true,"units":[],"playerRevision":57,"stashRevision":9})", transfer), "an empty transfer is refused");
        RequireStash(!KCDC::Features::Stash::StashContents::ReadTransfer(R"({"stash":700,"units":[{"id":"12","amount":1}],"playerRevision":57,"stashRevision":9})", transfer), "the direction is required");
        KCDC::Features::Inventory::InventoryIds ids;
        const KCDC::Features::Inventory::Inventory empty(ids);
        const auto snapshot = KCDC::Features::Stash::StashContents::Snapshot(700, empty, "notHolder");
        RequireStash(snapshot.at("stash") == 700 && snapshot.at("revision") == 0 && snapshot.at("code") == "notHolder" && snapshot.at("rows").empty(), "a snapshot names the container, revision, code and rows");
    }
} // namespace

void StashInventoryTests() {
    DepositAndWithdraw();
    StaleRevisionsAreRefused();
    ContainersHoldAtMostTheirCap();
    OnlyTheHolderMovesStock();
    LinkedChildrenShareTheirMaster();
    ScriptChangesAreReported();
    DestroyReportsInOrder();
    ViewsEnd();
    TransfersAreRead();
    HoldersKeepTheirView();
    VirtualAccessKeepsSessionChecks();
    StockOutlivesVirtualViews();
    OnlyAnOpenRevealsContents();
    ContentsAreKnownWithoutTheRows();
    DestroyKeepsUnreportedChanges();
    DropsDepositWithoutAHolder();
}
