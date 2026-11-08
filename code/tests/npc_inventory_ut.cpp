#include "features/npc/npc_inventory.h"
#include "shared/features/npc/npc_inventory_message.h"
#include "shared/game/animal_loot.h"
#include "shared/game/clothing_presets.h"
#include "shared/game/equipment_catalog.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/game/item_classes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
    constexpr const char *kSword = "ce1953df-9f10-4138-8fb1-393a11474553";
    constexpr const char *kSage  = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    void RequireNpc(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void CensusAndEquipment() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Npc::NpcInventory stock(ids);
        RequireNpc(!stock.Open(1, 1, 1, 0, false, false), "uninitialized native stock cannot be opened");
        RequireNpc(!stock.Seed({{{}, kSword, 2, {}}}, {3}), "worn units cannot exceed stock");
        RequireNpc(!stock.Seed({{"client-id", kSword, 2, {}}}, {1}), "native censuses cannot choose row identities");
        RequireNpc(!stock.Seed({{{}, kSage, 2, {}}}, {1}), "unequippable goods cannot become worn units");
        RequireNpc(!stock.Ready() && stock.Stock().Rows().empty(), "a refused census leaves no baseline");
        RequireNpc(stock.Seed({{{}, kSword, 2, {{"condition", 0.5}}}, {{}, kSage, 4, nlohmann::json::object()}}, {1, 0}), "initial inventory commits with canonical properties");
        RequireNpc(stock.Ready() && stock.Equipped().size() == 1 && stock.Stock().Rows().size() == 2, "stock includes loose and worn items");
        const auto saved = stock.Snapshot();
        RequireNpc(!stock.Seed({}, {}) && stock.Snapshot() == saved, "body rebuilds and simulator handovers cannot seed twice");
        RequireNpc(stock.Dress({kSword}) && stock.Stock().Rows().size() == 2, "dressing uses existing stock instead of duplicating it");
        RequireNpc(stock.Dress({}) && stock.Equipped().empty() && stock.Stock().Rows().size() == 2, "undressing retains inventory and cannot regenerate native clothes");
        RequireNpc(!stock.Dress({"unknown"}) && stock.Equipped().empty(), "an invalid outfit changes nothing");
        KCDC::Features::Npc::NpcInventory empty(ids);
        RequireNpc(empty.Seed({}, {}) && empty.Ready() && empty.Stock().Rows().empty(), "an empty inventory is a valid established baseline");
    }

    void EquippedAmmunitionCountsRows() {
        constexpr const char *arrows = "dfea5d01-b25c-414a-9ab4-6911a5f82118";
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Npc::NpcInventory archer(ids);
        KCDC::Features::Inventory::Inventory player(ids);
        RequireNpc(archer.Seed({{{}, arrows, 1000, nlohmann::json::object()}, {{}, kSword, 2, nlohmann::json::object()}}, {1000, 2}), "a full quiver is one equipped row, not a thousand rows");
        std::string quiver;
        for (const auto &[id, row] : archer.Stock().Rows()) {
            RequireNpc(archer.Equipped().at(id) == (row.itemClass == arrows ? 1000U : 1U), "ammunition preserves quantity while other gear wears one unit");
            if (row.itemClass == arrows) {
                quiver = id;
            }
        }
        RequireNpc(archer.Open(1, 10, 1, 100, false, false), "archer corpse opens");
        RequireNpc(archer.Transfer(player, 1, 10, 1, 101, {{quiver, 250}}, false, archer.Stock().Revision(), player.Revision()).ok, "partial quiver loots normally");
        RequireNpc(archer.Equipped().at(quiver) == 750 && archer.Stock().Rows().at(quiver).amount == 750, "remaining quiver quantity matches stock");
    }

    void OutfitsRetainCarriedContainers() {
        std::size_t checked = 0;
        for (const auto &preset : KCDC::Shared::Game::g_ClothingPresets) {
            std::vector<KCDC::Features::Inventory::InventoryRow> rows;
            std::vector<std::string> classes;
            bool hasContainer = false;
            for (const auto item : preset.itemGuids) {
                const auto *definition = KCDC::Shared::Game::FindItemClass(item);
                hasContainer           = hasContainer || (definition != nullptr && definition->category == "QuickSlotContainer");
                classes.emplace_back(item);
                rows.push_back({{}, std::string(item), 1, nlohmann::json::object()});
            }
            if (!hasContainer) {
                continue;
            }
            ++checked;
            KCDC::Features::Inventory::InventoryIds ids;
            KCDC::Features::Npc::NpcInventory stock(ids);
            RequireNpc(stock.Seed(rows, std::vector<std::uint32_t>(rows.size(), 1)), "authored outfits with containers establish their baseline");
            RequireNpc(stock.Ready() && stock.Stock().Rows().size() == rows.size() && !stock.Equipped().empty(), "all authored items remain lootable and supported equipment is worn");
            RequireNpc(stock.Dress({}) && stock.Dress(classes), "an initialized NPC accepts outfits containing belts and pouches");
            for (const auto &[id, row] : stock.Stock().Rows()) {
                if (KCDC::Shared::Game::FindItemClass(row.itemClass)->category == "QuickSlotContainer") {
                    RequireNpc(!stock.Equipped().contains(id), "containers remain carried stock without entering unsupported equipment slots");
                }
            }
            RequireNpc(stock.Stock().Rows().size() == rows.size(), "reapplying the outfit does not duplicate carried containers");
        }
        RequireNpc(checked == 30, "every shipped outfit containing a belt or pouch is covered");
    }

    void AcceptedStockFitsLootReplies() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Npc::NpcInventory stock(ids);
        RequireNpc(stock.Seed({}, {}), "large stock fixture initializes");
        const auto checkReply = [&]() {
            auto snapshot           = stock.Snapshot();
            snapshot["state"]       = UINT64_MAX;
            snapshot["nativeKnown"] = true;
            snapshot["harvestable"] = false;
            KCDC::Shared::RPC::NpcInventoryReply reply;
            reply.body = nlohmann::json({{"npc", UINT64_MAX}, {"epoch", UINT32_MAX}, {"request", UINT32_MAX}, {"action", 7U}, {"token", UINT64_MAX}, {"ok", true}, {"code", ""}, {"stock", snapshot}, {"playerRevision", UINT64_MAX}}).dump();
            MafiaNet::BitStream wire;
            reply.Serialize(&wire, true);
            RequireNpc(reply.valid, "every accepted stock fits the complete loot reply envelope");
            KCDC::Shared::RPC::NpcInventoryReply received;
            received.Serialize(&wire, false);
            RequireNpc(received.valid && received.body == reply.body, "large loot replies round trip without truncating stock");
            const auto decoded = nlohmann::json::parse(received.body).at("stock").at("rows");
            RequireNpc(decoded.size() == stock.Stock().Rows().size(), "compact wire rows retain every item");
            for (const auto &row : decoded) {
                const auto &original = stock.Stock().Rows().at(row.at("id").get<std::string>());
                RequireNpc(row.at("item") == original.itemClass && row.at("amount") == original.amount && row.at("metadata") == original.metadata, "compact rows preserve identity, quantity and canonical properties");
            }
        };
        std::vector<KCDC::Features::Inventory::InventoryRow> rows;
        for (std::size_t i = 0; i < 3000; ++i) {
            rows.push_back({"row" + std::to_string(i), kSage, 1, nlohmann::json::object()});
        }
        RequireNpc(stock.Stock().Set(rows, std::nullopt).ok, "the reported 3000-row sage inventory is accepted");
        RequireNpc(stock.Open(UINT64_MAX, UINT64_MAX, UINT32_MAX, 0, false, false), "large stock grants access");
        checkReply();
        rows.clear();
        for (std::size_t i = 0; i < KCDC::Features::Inventory::Inventory::kMaximumRows; ++i) {
            rows.push_back({std::string(60, 'x') + std::to_string(1000 + i), kSage, KCDC::Features::Inventory::Inventory::kMaximumAmount, nlohmann::json::object()});
        }
        std::size_t low = 0, high = rows.size();
        while (low < high) {
            const auto count = (low + high + 1) / 2;
            if (stock.Stock().Set({rows.begin(), rows.begin() + count}, std::nullopt).ok) {
                low = count;
            }
            else {
                high = count - 1;
            }
        }
        RequireNpc(low > 0 && low < rows.size() && stock.Stock().Set({rows.begin(), rows.begin() + low}, std::nullopt).ok, "long identities and maximum amounts reach the byte capacity boundary");
        checkReply();
        const auto before = stock.Snapshot();
        RequireNpc(!stock.Stock().Set({rows.begin(), rows.begin() + low + 1}, std::nullopt).ok && stock.Snapshot() == before, "the next row is refused without changing the sendable stock");
    }

    void ExclusiveAccessAndCancellation() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Npc::NpcInventory stock(ids);
        RequireNpc(stock.Seed({}, {}), "empty fixture initializes");
        RequireNpc(stock.Open(1, 10, 2, 100, true, true), "a harvest grant owns the corpse");
        RequireNpc(!stock.Open(2, 11, 2, 101, false, true), "another player cannot open a corpse being harvested");
        RequireNpc(!stock.Complete(2, 10, 2, 102) && !stock.Complete(1, 11, 2, 102) && !stock.Complete(1, 10, 3, 102), "completion requires the holder, token and incarnation");
        RequireNpc(!stock.Close(2, 10) && !stock.Close(1, 11), "a stale or foreign close cannot release someone else's access");
        RequireNpc(stock.Close(1, 10) && !stock.Harvested() && stock.Stock().Rows().empty(), "interruption grants nothing and leaves the carcass harvestable");
        RequireNpc(stock.Open(2, 12, 2, 200, true, true), "another player can harvest after cancellation");
        RequireNpc(!stock.Complete(2, 12, 2, 120200), "an expired grant cannot complete at the expiry boundary");
        RequireNpc(stock.Open(3, 13, 2, 120200, true, true), "expired access can be acquired by another player");
        RequireNpc(!stock.Renew(2, 12, 2, 120201), "a late renewal cannot reclaim superseded access");
        RequireNpc(stock.Snapshot().at("token") == 13U, "the published stock identifies its current access grant");
        stock.Release();
        RequireNpc(stock.Snapshot().at("token") == 0U, "access revocation is visible even when stock is unchanged");
        RequireNpc(stock.Holder() == 0 && !stock.Holds(3, 13, 2, 120201), "disconnect/destruction releases ownership");
    }

    void HarvestIsAtomicAndOncePerLife() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Npc::NpcInventory stock(ids);
        KCDC::Features::Inventory::Inventory player(ids);
        RequireNpc(stock.Seed({{{}, kSage, 4, nlohmann::json::object()}}, {0}), "gamemode prepares stock before the animal dies");
        const auto revision = stock.Stock().Revision();
        const auto id       = stock.Stock().Rows().begin()->first;
        RequireNpc(!stock.Open(1, 9, 1, 99, false, true), "prepared stock cannot bypass butchering");
        RequireNpc(stock.Open(1, 10, 1, 100, true, true), "butchering acquires prepared stock");
        RequireNpc(!stock.Transfer(player, 1, 10, 1, 101, {{id, 1}}, false, revision, 0).ok, "active butchering cannot transfer prepared stock");
        RequireNpc(stock.Complete(1, 10, 1, 102), "completion unlocks the prepared inventory");
        RequireNpc(stock.Stock().Revision() == revision && stock.Stock().Rows().at(id).amount == 4, "completion does not generate items or replace row identities");
        const auto saved = stock.Snapshot();
        RequireNpc(stock.Complete(1, 10, 1, 103) && stock.Snapshot() == saved, "a duplicate completion is idempotent");
        RequireNpc(stock.Close(1, 10) && !stock.Open(2, 11, 1, 104, true, true), "the same corpse cannot be harvested again");
        RequireNpc(stock.Open(2, 12, 1, 104, false, true), "harvested stock remains lootable");
        RequireNpc(stock.Transfer(player, 2, 12, 1, 105, {{id, 3}}, false, revision, 0).ok, "partial looting consumes prepared stock");
        stock.Revived();
        RequireNpc(!stock.Harvested() && stock.Holder() == 0 && stock.Stock().Rows().at(id).amount == 1, "revival retains remaining stock without rolling again");
        RequireNpc(!stock.Complete(2, 12, 1, 106), "an old-life completion cannot survive revival");
        RequireNpc(stock.Open(2, 13, 2, 106, true, true) && stock.Complete(2, 13, 2, 107), "a revived body unlocks its remaining stock");
        RequireNpc(stock.Stock().Rows().at(id).amount == 1, "a second life cannot replenish looted units");
    }

    void TransfersPreserveStockAndProperties() {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Npc::NpcInventory corpse(ids);
        KCDC::Features::Inventory::Inventory player(ids, 1);
        RequireNpc(corpse.Seed({{{}, kSword, 2, {{"condition", 0.5}}}, {{}, kSage, 4, nlohmann::json::object()}}, {1, 0}), "transfer fixture seeds");
        const auto sword      = corpse.Equipped().begin()->first;
        const auto properties = corpse.Stock().Rows().at(sword).metadata;
        RequireNpc(corpse.Open(1, 10, 1, 100, false, false), "corpse opens");
        const auto revision = corpse.Stock().Revision();
        RequireNpc(!corpse.Transfer(player, 2, 10, 1, 101, {{sword, 1}}, false, revision, 0).ok, "another player cannot transfer stock");
        RequireNpc(!corpse.Transfer(player, 1, 10, 1, 101, {{sword, 1}}, false, revision + 1, 0).ok, "a stale source revision is refused");
        RequireNpc(!corpse.Transfer(player, 1, 10, 1, 101, {{sword, 1}}, false, revision, 1).ok, "a stale destination revision is refused");
        RequireNpc(!corpse.Transfer(player, 1, 10, 1, 101, {{sword, 1}, {sword, 1}}, false, revision, 0).ok, "duplicate unit rows cannot overspend a selection");
        RequireNpc(corpse.Equipped().at(sword) == 1 && player.Rows().empty() && corpse.Stock().Revision() == revision, "failed transfers leave worn state and both inventories intact");
        const auto moved = corpse.Transfer(player, 1, 10, 1, 102, {{sword, 1}}, false, revision, 0);
        RequireNpc(moved.ok && corpse.Equipped().empty() && corpse.Stock().Rows().at(sword).amount == 1, "partial looting removes a worn unit without removing loose copies");
        RequireNpc(player.Rows().at(moved.outputs[0].id).metadata == properties, "quality, health and other canonical properties survive looting");
        std::string sage;
        for (const auto &[id, row] : corpse.Stock().Rows()) {
            if (row.itemClass == kSage) {
                sage = id;
            }
        }
        const auto before = corpse.Snapshot();
        RequireNpc(!corpse.Transfer(player, 1, 10, 1, 103, {{sage, 4}}, false, corpse.Stock().Revision(), player.Revision()).ok && corpse.Snapshot() == before, "a full destination refuses all items without losing corpse stock");
        const auto returned = corpse.Transfer(player, 1, 10, 1, 104, {{moved.outputs[0].id, 1}}, true, corpse.Stock().Revision(), player.Revision());
        RequireNpc(returned.ok && player.Rows().empty() && corpse.Stock().Rows().at(sword).amount == 2 && corpse.Equipped().empty(), "deposit preserves properties and never equips or regenerates removed clothes");
        RequireNpc(!corpse.Transfer(player, 1, 10, 1, 120104, {{sword, 1}}, false, corpse.Stock().Revision(), player.Revision()).ok, "an expired screen cannot loot");
    }

    void AccessAndProtocolBounds() {
        RequireNpc(KCDC::Features::Npc::NpcInventory::CanAccess(true, true, false, true, true, 4.0F), "exact use distance is permitted");
        for (const auto distance : {-1.0F, 4.001F, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            RequireNpc(!KCDC::Features::Npc::NpcInventory::CanAccess(true, true, false, true, true, distance), "invalid or distant positions cannot gain access");
        }
        RequireNpc(!KCDC::Features::Npc::NpcInventory::CanAccess(false, true, false, true, true, 1) && !KCDC::Features::Npc::NpcInventory::CanAccess(true, false, false, true, true, 1) && !KCDC::Features::Npc::NpcInventory::CanAccess(true, true, true, true, true, 1)
                       && !KCDC::Features::Npc::NpcInventory::CanAccess(true, true, false, false, true, 1) && !KCDC::Features::Npc::NpcInventory::CanAccess(true, true, false, true, false, 1),
            "readiness, living player, corpse, Lootable and world visibility are required");
        nlohmann::json request {{"npc", 1ULL}, {"epoch", 1U}, {"request", 1U}, {"action", 1U}, {"token", 0ULL}};
        std::uint64_t npc = 0, token = 0;
        std::uint32_t epoch = 0, sequence = 0;
        KCDC::Shared::NpcInventory::Action action;
        RequireNpc(KCDC::Shared::NpcInventory::ReadRequest(request, npc, epoch, sequence, action, token), "a bounded request parses");
        for (const char *key : {"npc", "epoch", "request", "action", "token"}) {
            auto bad = request;
            bad[key] = -1;
            RequireNpc(!KCDC::Shared::NpcInventory::ReadRequest(bad, npc, epoch, sequence, action, token), "negative wire fields cannot wrap into identities");
        }
        request["epoch"] = 0x100000000ULL;
        RequireNpc(!KCDC::Shared::NpcInventory::ReadRequest(request, npc, epoch, sequence, action, token), "wide incarnation numbers cannot truncate");
        RequireNpc(!KCDC::Shared::NpcInventory::ValidUnits(nlohmann::json::array()) && !KCDC::Shared::NpcInventory::ValidUnits(nlohmann::json::array({{{"id", "x"}, {"amount", 1U}}, {{"id", "x"}, {"amount", 1U}}})), "empty and duplicate selections are rejected");
    }

    void LootingUnderlayersKeepsTheBodyUsable() {
        constexpr const char *coif   = "e72e27a2-87db-480d-977b-318c34e0444f";
        constexpr const char *helmet = "2221b25c-a5a0-44ea-b96b-201781c1e7c4";
        const auto *coifClass        = KCDC::Shared::Game::FindEquipmentClass(coif);
        const auto *helmetClass      = KCDC::Shared::Game::FindEquipmentClass(helmet);
        RequireNpc(coifClass != nullptr && helmetClass != nullptr && helmetClass->requiredSlots == coifClass->slots && coifClass->equipOrder < helmetClass->equipOrder, "QA coif is the helmet's required underlayer and is projected first");

        std::set<std::uint64_t> coveredSlots;
        for (const auto &outer : KCDC::Shared::Game::g_EquipmentClasses) {
            const auto *properties = KCDC::Shared::Game::FindInventoryPropertyClass(outer.guid);
            if (outer.requiredSlots == 0 || properties == nullptr || properties->questItem || coveredSlots.contains(outer.slots)) {
                continue;
            }
            const auto inner = std::find_if(KCDC::Shared::Game::g_EquipmentClasses.begin(), KCDC::Shared::Game::g_EquipmentClasses.end(), [&](const auto &item) {
                const auto *candidate = KCDC::Shared::Game::FindInventoryPropertyClass(item.guid);
                return candidate != nullptr && !candidate->questItem && item.requiredSlots == 0 && (item.slots & outer.requiredSlots) == outer.requiredSlots && outer.CanWear(item.slots | outer.slots);
            });
            RequireNpc(inner != KCDC::Shared::Game::g_EquipmentClasses.end() && inner->equipOrder < outer.equipOrder, "every authored dependent slot has a usable underlayer and stable dressing order");
            coveredSlots.insert(outer.slots);

            KCDC::Features::Inventory::InventoryIds ids;
            KCDC::Features::Npc::NpcInventory corpse(ids);
            KCDC::Features::Inventory::Inventory player(ids);
            // Outer first deliberately reverses the row order. One loose
            // spare underlayer must not keep the outer item equipped.
            RequireNpc(corpse.Seed({{{}, std::string(outer.guid), 1, nlohmann::json::object()}, {{}, std::string(inner->guid), 2, nlohmann::json::object()}, {{}, kSage, 7, nlohmann::json::object()}}, {1, 1, 0}), "layered corpse seeds in either row order");
            std::string outerId, innerId, sageId;
            for (const auto &[id, row] : corpse.Stock().Rows()) {
                if (row.itemClass == outer.guid) {
                    outerId = id;
                }
                if (row.itemClass == inner->guid) {
                    innerId = id;
                }
                if (row.itemClass == kSage) {
                    sageId = id;
                }
            }
            RequireNpc(corpse.Equipped().size() == 2 && corpse.Open(1, 10, 1, 100, false, false), "both layers are worn before looting");
            const auto before = corpse.Snapshot();
            RequireNpc(!corpse.Transfer(player, 1, 10, 1, 101, {{innerId, 1}}, false, corpse.Stock().Revision() + 1, player.Revision()).ok && corpse.Snapshot() == before, "refused transfer must not unequip dependent gear");
            RequireNpc(corpse.Transfer(player, 1, 10, 1, 102, {{innerId, 1}}, false, corpse.Stock().Revision(), player.Revision()).ok, "first loot removes the worn underlayer");
            RequireNpc(corpse.Equipped().empty() && corpse.Stock().Rows().at(outerId).amount == 1 && corpse.Stock().Rows().at(innerId).amount == 1, "dependent gear becomes loose stock without consuming it or equipping a spare");
            RequireNpc(corpse.Transfer(player, 1, 10, 1, 103, {{outerId, 1}}, false, corpse.Stock().Revision(), player.Revision()).ok, "second loot takes the now-unworn outer item in the same session");
            RequireNpc(corpse.Close(1, 10) && corpse.Open(1, 11, 1, 104, false, false), "the same partially looted corpse reopens");
            RequireNpc(corpse.Transfer(player, 1, 11, 1, 105, {{sageId, 3}}, false, corpse.Stock().Revision(), player.Revision()).ok && corpse.Stock().Rows().at(sageId).amount == 4, "later partial-stack looting remains available");
            RequireNpc(corpse.Dress({std::string(outer.guid), std::string(inner->guid)}) && corpse.Equipped().size() == 2, "scripted dressing restores both layers from stock or newly authored items");
            RequireNpc(corpse.Stock().Remove({{innerId, 1}}, std::nullopt).ok, "script removes the remaining underlayer");
            corpse.ClampEquipment();
            RequireNpc(corpse.Equipped().empty(), "scripted removal applies the same dependency rule");
        }
        RequireNpc(coveredSlots.size() == 8, "seven dependent slots, including both leg-armor coverage variants, are all exercised");
    }

    void AnimalCatalog() {
        for (const auto &animal : KCDC::Shared::Game::g_AnimalLoot) {
            for (const auto &item : animal.items) {
                RequireNpc(KCDC::Shared::Game::FindItemClass(item.item) != nullptr && item.amount > 0 && std::isfinite(item.fraction) && std::isfinite(item.variation), "every harvested part names a real item and bounded amount");
            }
        }
        const auto *hare = KCDC::Shared::Game::FindAnimalLoot("Hare");
        const auto *wolf = KCDC::Shared::Game::FindAnimalLoot("Wolf");
        RequireNpc(hare != nullptr && hare->items.size() == 3 && hare->items[0].amount == 8, "hare stock includes meat, liver and skin");
        RequireNpc(wolf != nullptr && wolf->items.size() == 4 && wolf->items[0].amount == 20, "wolf stock includes meat, skin, ear and teeth");
        RequireNpc(KCDC::Shared::Game::FindAnimalLoot("Dog")->items.size() == KCDC::Shared::Game::FindAnimalLoot("WildDog")->items.size(), "wild dogs use the game's dog loot rule");
        RequireNpc(KCDC::Shared::Game::FindAnimalLoot("Hen")->items.empty() && KCDC::Shared::Game::FindAnimalLoot("NPC") == nullptr, "empty and non-animal presets remain distinct");
        bool exception = false;
        for (const auto &soul : KCDC::Shared::Game::g_AnimalSouls) {
            if (!soul.lootable) {
                exception = true;
            }
        }
        RequireNpc(exception, "the named quest white-deer exclusion is compiled");
        for (const auto &soul : KCDC::Shared::Game::g_AnimalSouls) {
            const auto *native = KCDC::Shared::Game::FindAnimalSoulByName(soul.name);
            RequireNpc(native != nullptr && native->actorClass == soul.actorClass && native->lootable == soul.lootable, "adopted animals resolve by their native soul identity without guessing from class names");
        }
    }
} // namespace

void NpcInventoryTests() {
    CensusAndEquipment();
    EquippedAmmunitionCountsRows();
    OutfitsRetainCarriedContainers();
    AcceptedStockFitsLootReplies();
    ExclusiveAccessAndCancellation();
    HarvestIsAtomicAndOncePerLife();
    TransfersPreserveStockAndProperties();
    LootingUnderlayersKeepsTheBodyUsable();
    AccessAndProtocolBounds();
    AnimalCatalog();
    KCDC::Features::Inventory::InventoryIds ids;
    KCDC::Features::Npc::NpcInventory restored(ids);
    RequireNpc(restored.Seed({}, {}) && restored.Open(1, 10, 1, 100, true, true), "restore fixture grants harvesting");
    restored.Restored({}, true);
    RequireNpc(restored.Harvested() && restored.Holder() == 0 && !restored.Complete(1, 10, 1, 101), "restoring a harvested corpse cancels the old lease and never grants a second yield");
}

void NpcHarvestSyncTests() {
    KCDC::Features::Inventory::InventoryIds ids;
    KCDC::Features::Npc::NpcInventory stock(ids);
    KCDC::Shared::NpcInventory::InteractionState observer;
    RequireNpc(stock.Seed({}, {}) && observer.Read(nlohmann::json::parse(stock.Snapshot().dump())), "an idle corpse publishes a valid interaction snapshot");
    RequireNpc(!observer.harvesting && !observer.harvested && !observer.BusyFor(0), "fresh animals offer butchering to observers");
    RequireNpc(stock.Open(1, 10, 1, 100, true, true) && observer.Read(stock.Snapshot()), "the grant reaches observers before the action begins");
    RequireNpc(observer.harvesting && !observer.harvested && observer.holder == 1 && observer.BusyFor(0) && observer.BusyFor(11) && !observer.BusyFor(10), "only the grant owner can use a busy body");
    const auto running = observer;
    RequireNpc(!stock.Close(2, 10) && stock.Interaction() == running, "foreign cancellation cannot change the shared action state");
    RequireNpc(stock.Close(1, 10) && observer.Read(stock.Snapshot()) && !observer.harvesting && !observer.harvested && !observer.BusyFor(0), "cancellation unlocks butchering without publishing a harvest");
    RequireNpc(!stock.Complete(1, 10, 1, 101), "delayed completion after cancellation cannot change the prompt");
    RequireNpc(stock.Open(2, 11, 1, 102, true, true), "the next player can restart butchering");
    const auto inventoryRevision = stock.Stock().Revision();
    RequireNpc(stock.Complete(2, 11, 1, 103) && observer.Read(stock.Snapshot()), "zero-yield completion still publishes the prompt transition");
    RequireNpc(observer.harvested && !observer.harvesting && observer.BusyFor(0) && stock.Stock().Revision() == inventoryRevision && observer != running, "completion changes interaction state even when stock and grant do not change");
    KCDC::Shared::NpcInventory::InteractionState lateObserver;
    RequireNpc(lateObserver.Read(nlohmann::json::parse(stock.Snapshot().dump())) && lateObserver == observer, "streaming in reconstructs the completed state without replaying the action");
    stock.Release();
    RequireNpc(observer.Read(stock.Snapshot()) && observer.harvested && !observer.BusyFor(0), "disconnect or closing releases access while retaining completion");
    stock.Revived();
    RequireNpc(observer.Read(stock.Snapshot()) && !observer.harvested && !observer.harvesting, "revival clears the previous life's butcher state");
    RequireNpc(stock.Open(3, 12, 2, 200, true, true) && !stock.Holds(3, 12, 2, 120200), "the active butcher grant expires at the lease boundary");
    stock.Release();
    RequireNpc(observer.Read(stock.Snapshot()) && !observer.harvested && !observer.harvesting && !observer.BusyFor(0), "expired access restores the unharvested prompt");
    RequireNpc(stock.Open(3, 13, 2, 120201, true, true), "restore fixture owns a new grant");
    stock.Restored({}, true);
    RequireNpc(observer.Read(stock.Snapshot()) && observer.harvested && !observer.harvesting && observer.holder == 0, "restoring a completed corpse cancels the in-flight action");
    const auto valid = stock.Snapshot();
    for (const auto *field : {"holder", "token", "harvesting", "harvested"}) {
        auto malformed = valid;
        malformed.erase(field);
        const auto before = observer;
        RequireNpc(!observer.Read(malformed) && observer == before, "partial snapshots cannot leave mixed interaction state");
    }
    auto malformed          = valid;
    malformed["harvesting"] = true;
    RequireNpc(!observer.Read(malformed), "a completed body cannot simultaneously be undergoing butchering");
    malformed          = valid;
    malformed["token"] = 99U;
    RequireNpc(!observer.Read(malformed), "a grant without its owner is invalid");
}
