#include "shared/rate_limit.h"
#include "features/items/inventory_notifications.h"
#include "shared/features/inventory/inventory_message.h"
#include "shared/features/inventory/inventory_sync.h"
#include "shared/game/item_classes.h"

#include <mafianet/bit_stream.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
    constexpr const char *kSage = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";

    void RequireSync(bool value, const char *message) {
        if (!value) {
            throw std::runtime_error(message);
        }
    }

    nlohmann::json Row(std::uint32_t amount, double health = 1.0) {
        return {{"item", kSage}, {"amount", amount}, {"metadata", {{"quality", 1}, {"health", health}, {"condition", health}}}};
    }

    void FullSnapshotReplacesRows() {
        std::map<std::string, nlohmann::json> server = {{"1", Row(2)}, {"2", Row(1, 0.5)}};
        std::map<std::string, nlohmann::json> client = {{"stale", Row(9)}};
        std::uint64_t revision                       = 40;
        const auto full                              = KCDC::Shared::Inventory::EncodeSync(std::nullopt, 3, {}, server);
        RequireSync(KCDC::Shared::Inventory::ApplySync(full, revision, client) && revision == 3 && client == server, "a full snapshot replaces whatever the client held");
    }

    void DeltaCarriesOnlyChanges() {
        const std::map<std::string, nlohmann::json> before = {{"1", Row(2)}, {"2", Row(1, 0.5)}, {"3", Row(4)}};
        const std::map<std::string, nlohmann::json> after  = {{"1", Row(2)}, {"2", Row(1, 0.25)}, {"4", Row(7)}};
        const auto delta                                   = KCDC::Shared::Inventory::EncodeSync(5, 6, before, after);
        RequireSync(!delta.contains("rows") && delta.at("upsert").size() == 2 && delta.at("remove") == nlohmann::json::array({"3"}), "a delta names only upserted and removed rows");

        auto client            = before;
        std::uint64_t revision = 5;
        RequireSync(KCDC::Shared::Inventory::ApplySync(delta, revision, client) && revision == 6 && client == after, "a delta applied to its base reproduces the server rows");
    }

    void DeltaNeedsItsBase() {
        std::map<std::string, nlohmann::json> client = {{"1", Row(2)}};
        const auto original                          = client;
        std::uint64_t revision                       = 4;
        const auto delta                             = KCDC::Shared::Inventory::EncodeSync(5, 6, {}, {{"9", Row(1)}});
        RequireSync(!KCDC::Shared::Inventory::ApplySync(delta, revision, client) && revision == 4 && client == original, "a delta on the wrong base is refused and applies nothing");
    }

    void MalformedSyncIsRefused() {
        std::map<std::string, nlohmann::json> client;
        std::uint64_t revision = 0;
        for (const auto &bad : {nlohmann::json::array(), nlohmann::json {{"full", true}}, nlohmann::json {{"full", true}, {"revision", 1}, {"rows", {{"1", {{"item", kSage}, {"amount", -1}, {"metadata", nlohmann::json::object()}}}}}},
                 nlohmann::json {{"full", true}, {"revision", 1}, {"rows", {{std::string(65, 'x'), Row(1)}}}}, nlohmann::json {{"full", true}, {"revision", 1}, {"rows", {{"1", {{"item", 5}, {"amount", 1}, {"metadata", nlohmann::json::object()}}}}}}}) {
            RequireSync(!KCDC::Shared::Inventory::ApplySync(bad, revision, client) && client.empty() && revision == 0, "malformed sync is refused whole");
        }
    }

    void MessageReadNeverAllocatesBeyondThePacket() {
        MafiaNet::BitStream stream;
        std::uint32_t claimed = KCDC::Shared::RPC::InventoryMessage::kMaximumBytes;
        stream.Write(claimed);
        stream.Write('x');
        KCDC::Shared::RPC::InventoryMessage message;
        message.Serialize(&stream, false);
        RequireSync(!message.valid && message.body.capacity() < 1024, "a length the packet does not carry is refused before any allocation");
    }

    void RateLimitDropsThenFloods() {
        KCDC::Shared::RateLimit limit(10.0, 4.0);
        std::uint64_t now = 1000;
        for (int i = 0; i < 4; ++i) {
            RequireSync(limit.Take(now), "a burst up to its size passes");
        }
        RequireSync(!limit.Take(now), "a burst beyond its size is refused");
        now += 100;
        RequireSync(limit.Take(now) && !limit.Take(now), "tokens come back at the configured rate");
        now += 10000;
        for (int i = 0; i < 4; ++i) {
            RequireSync(limit.Take(now), "a long pause refills to the burst, never beyond");
        }
        RequireSync(!limit.Take(now), "the refill is capped at the burst");
    }

    void ItemClassLookupIsExact() {
        for (const auto &entry : KCDC::Shared::Game::g_ItemClasses) {
            const auto *found = KCDC::Shared::Game::FindItemClass(entry.guid);
            RequireSync(found != nullptr && found->guid == entry.guid && found <= &entry, "every shipped class is found by its own GUID, first table entry first");
        }
        RequireSync(KCDC::Shared::Game::FindItemClass("00000000-0000-0000-0000-000000000001") == nullptr && KCDC::Shared::Game::FindItemClass("") == nullptr, "an unknown GUID is not found");
    }

    nlohmann::json Notices(std::initializer_list<std::pair<const std::string, std::int64_t>> notices) {
        return {{"notices", KCDC::Shared::Inventory::EncodeNotices(notices)}};
    }

    void NoticesTravelWithASync() {
        std::map<std::string, std::int64_t> read;
        RequireSync(KCDC::Shared::Inventory::ReadNotices(nlohmann::json::object(), read) && read.empty(), "a sync without notices announces nothing");
        RequireSync(KCDC::Shared::Inventory::ReadNotices(Notices({{kSage, 8}, {"other", -3}}), read) && read == std::map<std::string, std::int64_t> {{kSage, 8}, {"other", -3}}, "notices read back as encoded, losses negative");
        for (const auto &bad : {nlohmann::json {{"notices", nlohmann::json::object()}}, nlohmann::json {{"notices", {{{"item", kSage}, {"amount", 0}}}}}, nlohmann::json {{"notices", {{{"item", kSage}, {"amount", 1.5}}}}},
                 nlohmann::json {{"notices", {{{"item", 5}, {"amount", 1}}}}}, nlohmann::json {{"notices", {{{"item", kSage}, {"amount", 1}}, {{"item", kSage}, {"amount", 2}}}}},
                 nlohmann::json {{"notices", {{{"item", kSage}, {"amount", KCDC::Shared::Inventory::kMaximumSyncNotice + 1}}}}}, nlohmann::json {{"notices", {{{"item", kSage}, {"amount", 18446744073709551615ULL}}}}}}) {
            RequireSync(!KCDC::Shared::Inventory::ReadNotices(bad, read) && read.empty(), "a malformed notice list is refused whole");
        }
    }

    void NotificationsShowWhatTheServerAnnounced() {
        KCDC::Features::Items::InventoryNotifications notifications;
        std::vector<std::pair<std::string, int>> shown;
        const auto show = [&](const std::string &itemClass, int amount) {
            shown.push_back({itemClass, amount});
            return true;
        };
        notifications.Queue(nlohmann::json::object());
        notifications.Flush(show);
        RequireSync(shown.empty(), "a sync the server did not announce shows nothing, however its rows moved");

        notifications.Queue(Notices({{kSage, 8}}));
        notifications.Flush(show);
        notifications.Flush(show);
        RequireSync(shown == std::vector<std::pair<std::string, int>> {{kSage, 8}}, "an announced gain shows once");

        notifications.Queue(Notices({{kSage, -3}, {"other", 2}}));
        notifications.Flush(show);
        RequireSync(shown.size() == 3 && shown[1] == std::pair<std::string, int> {kSage, -3} && shown[2] == std::pair<std::string, int> {"other", 2}, "classes show separately, losses negative");

        notifications.Queue(nlohmann::json {{"notices", "garbage"}});
        notifications.Flush(show);
        RequireSync(shown.size() == 3, "a malformed list shows nothing");
    }

    void NotificationsWaitForTheHud() {
        KCDC::Features::Items::InventoryNotifications notifications;
        std::vector<int> shown;
        notifications.Queue(Notices({{kSage, 8}}));
        notifications.Flush([](const std::string &, int) {
            return false;
        });
        notifications.Queue(Notices({{kSage, -2}}));
        notifications.Flush([&](const std::string &, int amount) {
            shown.push_back(amount);
            return true;
        });
        RequireSync(shown == std::vector<int> {6}, "a missing HUD keeps notices, and later ones join them as the net change");

        notifications.Queue(Notices({{kSage, 4}}));
        notifications.Reset();
        notifications.Flush([&](const std::string &, int amount) {
            shown.push_back(amount);
            return true;
        });
        RequireSync(shown.size() == 1, "a session end drops waiting notices");
    }

    void NotificationsBoundNativeCounts() {
        KCDC::Features::Items::InventoryNotifications notifications;
        const std::int64_t maximum = std::numeric_limits<int>::max();
        notifications.Queue(Notices({{kSage, maximum * 2}}));
        std::vector<int> shown;
        notifications.Flush([&](const std::string &, int amount) {
            shown.push_back(amount);
            return shown.size() == 1;
        });
        RequireSync(shown == std::vector<int>({static_cast<int>(maximum), static_cast<int>(maximum)}), "large counts are split without signed overflow");
        shown.clear();
        notifications.Flush([&](const std::string &, int amount) {
            shown.push_back(amount);
            return true;
        });
        RequireSync(shown == std::vector<int> {static_cast<int>(maximum)}, "a partial flush retries only the undelivered count");
        notifications.Queue(Notices({{kSage, -maximum * 2}}));
        shown.clear();
        notifications.Flush([&](const std::string &, int amount) {
            shown.push_back(amount);
            return true;
        });
        RequireSync(shown == std::vector<int>({-static_cast<int>(maximum), -static_cast<int>(maximum)}), "large losses keep their direction and native count bounds");
    }
} // namespace

void InventorySyncTests() {
    FullSnapshotReplacesRows();
    DeltaCarriesOnlyChanges();
    DeltaNeedsItsBase();
    MalformedSyncIsRefused();
    MessageReadNeverAllocatesBeyondThePacket();
    RateLimitDropsThenFloods();
    ItemClassLookupIsExact();
    NoticesTravelWithASync();
    NotificationsShowWhatTheServerAnnounced();
    NotificationsWaitForTheHud();
    NotificationsBoundNativeCounts();
}
