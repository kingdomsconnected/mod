#include "inventory_gameplay.h"

#include "shared/game/consumable_catalog.h"
#include "shared/game/item_classes.h"
#include "shared/game/missile_catalog.h"
#include "shared/pose_math.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <string_view>

namespace KCDC::Features::Inventory {
    bool SpentRounds::ReadRecovery(const nlohmann::json &message, AmmunitionRecovery &recovery) {
        recovery = {};
        if (!message.is_object()) {
            return false;
        }
        const auto fired    = message.find("item");
        const auto landed   = message.find("landed");
        const auto position = message.find("position");
        if (fired == message.end() || !fired->is_string() || landed == message.end() || !landed->is_string() || !LandsAs(fired->get<std::string>(), landed->get<std::string>()) || position == message.end() || !position->is_array() || position->size() != 3) {
            return false;
        }
        for (const auto &coordinate : *position) {
            if (!coordinate.is_number()) {
                return false;
            }
        }
        recovery.fired    = fired->get<std::string>();
        recovery.landed   = landed->get<std::string>();
        recovery.position = {position->at(0).get<float>(), position->at(1).get<float>(), position->at(2).get<float>()};
        if (!Shared::Game::IsFinite(recovery.position)) {
            return false;
        }
        const auto nativeItem = message.find("nativeItemWuid");
        const auto rotation   = message.find("rotation");
        if (nativeItem != message.end()) {
            if (!nativeItem->is_number_unsigned() || nativeItem->get<std::uint64_t>() == 0 || rotation == message.end()) {
                return false;
            }
            recovery.nativeItemWuid = nativeItem->get<std::uint64_t>();
        }
        if (rotation != message.end()) {
            if (!rotation->is_array() || rotation->size() != 4) {
                return false;
            }
            for (const auto &coordinate : *rotation) {
                if (!coordinate.is_number()) {
                    return false;
                }
            }
            recovery.rotation         = {rotation->at(0).get<float>(), rotation->at(1).get<float>(), rotation->at(2).get<float>(), rotation->at(3).get<float>()};
            const float lengthSquared = glm::dot(recovery.rotation, recovery.rotation);
            if (!Shared::Game::IsFinite(recovery.rotation) || lengthSquared < 0.25F || lengthSquared > 4.0F) {
                return false;
            }
            recovery.rotation = glm::normalize(recovery.rotation);
        }
        return true;
    }

    namespace {
        std::string_view Category(const std::string &itemClass) {
            const auto *entry = KCDC::Shared::Game::FindItemClass(itemClass);
            return entry != nullptr ? entry->category : std::string_view {};
        }

        bool ReadRepairPart(const nlohmann::json &part, bool kit, InventoryRepairPart &output) {
            if (!part.is_object() || !part.contains("id") || !part.at("id").is_string() || !part.contains("before") || !part.at("before").is_object() || !part.contains("after") || !part.at("after").is_object()) {
                return false;
            }
            if (kit && (!part.contains("spent") || !part.at("spent").is_boolean())) {
                return false;
            }
            output = {part.at("id").get<std::string>(), part.at("before"), part.at("after"), kit && part.at("spent").get<bool>()};
            return true;
        }

        std::uint32_t Worn(const std::map<std::string, std::uint32_t> &equipped, const std::string &id) {
            const auto found = equipped.find(id);
            return found == equipped.end() ? 0 : found->second;
        }

        /** A message's `units: [{ id, amount }]`: one to sixteen distinct rows. */
        bool ReadOwnerUnits(const nlohmann::json &message, std::vector<InventoryUnit> &units) {
            const auto found = message.find("units");
            if (found == message.end() || !found->is_array() || found->empty() || found->size() > InventoryGameplay::kMaximumConsumedUnits) {
                return false;
            }
            std::vector<InventoryUnit> parsed;
            std::set<std::string> seen;
            for (const auto &unit : *found) {
                if (!unit.is_object() || !unit.contains("id") || !unit.at("id").is_string() || !unit.contains("amount") || !unit.at("amount").is_number_unsigned()) {
                    return false;
                }
                const auto amount = unit.at("amount").get<std::uint64_t>();
                if (amount == 0 || amount > Inventory::kMaximumAmount || !seen.insert(unit.at("id").get<std::string>()).second) {
                    return false;
                }
                parsed.push_back({unit.at("id").get<std::string>(), static_cast<std::uint32_t>(amount)});
            }
            units = std::move(parsed);
            return true;
        }
    } // namespace

    bool InventoryGameplay::IsEquippableClass(const std::string &itemClass) {
        return Shared::Game::IsEquippableCategory(Category(itemClass));
    }

    bool InventoryGameplay::IsClothingClass(const std::string &itemClass) {
        const auto category = Category(itemClass);
        return category == "Armor" || category == "Helmet" || category == "Hood";
    }

    bool InventoryGameplay::IsConsumableClass(const std::string &itemClass, InventoryConsumption kind) {
        const auto category = Category(itemClass);
        if (kind == InventoryConsumption::Shot) {
            return category == "Ammo";
        }
        return category == "Food" || category == "Ointment";
    }

    const char *InventoryGameplay::UseKind(const std::string &itemClass) {
        const auto *entry = KCDC::Shared::Game::FindItemClass(itemClass);
        if (entry == nullptr) {
            return "food";
        }
        if (entry->category == "Ammo") {
            return "shot";
        }
        if (entry->category == "Ointment") {
            return "ointment";
        }
        const auto *food = KCDC::Shared::Game::FindConsumable(itemClass);
        return food != nullptr && food->potion ? "potion" : "food";
    }

    bool InventoryGameplay::ReadConsume(const nlohmann::json &message, std::string &request, InventoryConsumption &kind, std::vector<InventoryUnit> &units) {
        if (!message.is_object() || !message.contains("request") || !message.at("request").is_string() || !message.contains("kind") || !message.at("kind").is_string()) {
            return false;
        }
        const auto &name = message.at("kind").get_ref<const std::string &>();
        if ((name != "use" && name != "shot") || !ReadOwnerUnits(message, units)) {
            return false;
        }
        request = message.at("request").get<std::string>();
        kind    = name == "shot" ? InventoryConsumption::Shot : InventoryConsumption::Use;
        return true;
    }

    bool InventoryGameplay::ReadPreparation(const nlohmann::json &message, std::string &request, KCDC::Shared::Game::PreparationKind &kind, std::vector<InventoryUnit> &units) {
        if (!message.is_object() || !message.contains("request") || !message.at("request").is_string() || !message.contains("kind") || (message.at("kind") != "dry" && message.at("kind") != "smoke" && message.at("kind") != "cook") || !ReadOwnerUnits(message, units)) {
            return false;
        }
        request = message.at("request").get<std::string>();
        kind    = message.at("kind") == "cook" ? KCDC::Shared::Game::PreparationKind::Cooking : message.at("kind") == "smoke" ? KCDC::Shared::Game::PreparationKind::Smoking : KCDC::Shared::Game::PreparationKind::Drying;
        return true;
    }

    std::vector<InventoryCredit> InventoryGameplay::Prepared(const Inventory &inventory, KCDC::Shared::Game::PreparationKind kind, const std::vector<InventoryUnit> &units) {
        std::vector<InventoryCredit> products;
        for (const auto &unit : units) {
            const auto row       = inventory.Rows().find(unit.id);
            const auto *prepared = row != inventory.Rows().end() ? KCDC::Shared::Game::FindPreparedItem(kind, row->second.itemClass) : nullptr;
            if (prepared == nullptr) {
                return {};
            }
            // The game makes a dried herb at full health (0x182CA9C3C), and
            // dried, smoked or cooked food at the health the food had
            // (0x182CA9C44, for a soul without ability 95): food keeps its
            // row's properties.
            products.push_back({std::string(prepared->product), unit.amount, prepared->herb ? nlohmann::json::object() : row->second.metadata});
        }
        return products;
    }

    bool InventoryGameplay::ReadRepair(const nlohmann::json &message, std::string &request, std::vector<InventoryRepairPart> &kits, std::vector<InventoryRepairPart> &equipment) {
        if (!message.is_object() || !message.contains("request") || !message.at("request").is_string() || !message.contains("equipment") || !message.contains("kits")) {
            return false;
        }
        const auto &parts = message.at("kits");
        const auto &items = message.at("equipment");
        if (!parts.is_array() || parts.empty() || parts.size() > Inventory::kMaximumRepairKits) {
            return false;
        }
        std::vector<InventoryRepairPart> parsedEquipment;
        const auto readEquipment = [&](const nlohmann::json &item, const nlohmann::json &flags) {
            InventoryRepairPart part;
            if (!ReadRepairPart(item, false, part) || !flags.contains("equipped") || !flags.at("equipped").is_boolean()) {
                return false;
            }
            part.equipped = flags.at("equipped").get<bool>();
            parsedEquipment.push_back(std::move(part));
            return true;
        };
        if (items.is_array()) {
            if (items.empty() || items.size() > KCDC::Shared::Inventory::kMaximumRepairEquipment) {
                return false;
            }
            for (const auto &item : items) {
                if (!readEquipment(item, item)) {
                    return false;
                }
            }
        }
        else if (!readEquipment(items, message)) {
            return false;
        }
        std::vector<InventoryRepairPart> parsedKits(parts.size());
        for (std::size_t index = 0; index < parts.size(); ++index) {
            if (!ReadRepairPart(parts.at(index), true, parsedKits[index])) {
                return false;
            }
        }
        request   = message.at("request").get<std::string>();
        kits      = std::move(parsedKits);
        equipment = std::move(parsedEquipment);
        return true;
    }

    bool InventoryGameplay::ReadSharpen(const nlohmann::json &message, std::string &request, InventoryRepairPart &weapon) {
        if (!message.is_object() || !message.contains("request") || !message.at("request").is_string() || !message.contains("equipment") || !message.contains("equipped") || !message.at("equipped").is_boolean()) {
            return false;
        }
        if (!ReadRepairPart(message.at("equipment"), false, weapon)) {
            return false;
        }
        weapon.equipped = message.at("equipped").get<bool>();
        request         = message.at("request").get<std::string>();
        return true;
    }

    InventoryResult InventoryGameplay::Consume(Inventory &inventory, InventoryConsumption kind, const std::set<std::string> &loaded, const std::vector<InventoryUnit> &units, std::vector<InventoryRow> *spent) {
        if (units.empty() || units.size() > kMaximumConsumedUnits) {
            return {false, "invalidItems"};
        }
        const std::uint32_t limit = kind == InventoryConsumption::Shot ? kMaximumShotAmount : kMaximumUseAmount;
        std::uint32_t total       = 0;
        std::vector<InventoryRow> rows;
        for (const auto &unit : units) {
            total += unit.amount;
            if (unit.amount == 0 || total > limit) {
                return {false, "invalidAmount"};
            }
            const auto row = inventory.Rows().find(unit.id);
            if (row == inventory.Rows().end()) {
                return {false, "unknownItem"};
            }
            if (!IsConsumableClass(row->second.itemClass, kind)) {
                return {false, "notConsumable"};
            }
            if (kind == InventoryConsumption::Shot && !loaded.contains(unit.id)) {
                return {false, "notLoaded"};
            }
            rows.push_back({row->second.id, row->second.itemClass, unit.amount, row->second.metadata});
        }
        // The named units bound the spend on their own: wear moves the revision
        // all through a fight, and the meal or the shot has already happened.
        auto result = inventory.Remove(units, std::nullopt, kind == InventoryConsumption::Shot ? "shot" : "use");
        if (result.ok && spent != nullptr) {
            *spent = std::move(rows);
        }
        return result;
    }

    bool InventoryGameplay::ReadEquipment(const nlohmann::json &message, std::uint64_t &revision, std::map<std::string, std::uint32_t> &items, std::uint64_t equipmentRestore) {
        if (!message.is_object() || !message.contains("revision") || !message.at("revision").is_number_unsigned()) {
            return false;
        }
        if (const auto restore = message.find("equipmentRestore"); restore != message.end() && (!restore->is_number_unsigned() || restore->get<std::uint64_t>() != equipmentRestore)) {
            return false;
        }
        const auto found = message.find("items");
        if (found == message.end() || !found->is_object() || found->size() > kMaximumEquippedRows) {
            return false;
        }
        std::map<std::string, std::uint32_t> parsed;
        for (const auto &[id, amount] : found->items()) {
            if (id.empty() || id.size() > Inventory::kMaximumIdLength || !amount.is_number_unsigned() || amount.get<std::uint64_t>() == 0 || amount.get<std::uint64_t>() > Inventory::kMaximumAmount) {
                return false;
            }
            parsed.emplace(id, static_cast<std::uint32_t>(amount.get<std::uint64_t>()));
        }
        revision = message.at("revision").get<std::uint64_t>();
        items    = std::move(parsed);
        return true;
    }

    std::map<std::string, std::uint32_t> InventoryGameplay::CountEquipment(const Inventory &inventory, const std::map<std::string, std::uint32_t> &items) {
        struct Quiver {
            std::uint64_t amount = 0;
            std::map<std::string, std::uint32_t> rows;
        };
        std::map<std::string, std::uint32_t> equipped;
        std::map<std::string, Quiver> quivers;
        std::map<std::uint32_t, const Quiver *> selected;
        for (const auto &[id, amount] : items) {
            const auto row = inventory.Rows().find(id);
            if (row == inventory.Rows().end() || !IsEquippableClass(row->second.itemClass)) {
                continue;
            }
            if (Category(row->second.itemClass) != "Ammo") {
                equipped.emplace(id, 1);
                continue;
            }
            const std::uint32_t worn = std::min(amount, row->second.amount);
            if (worn == 0) {
                continue;
            }
            // Inventory metadata is canonical native properties. The client
            // projects equal class/properties rows into one physical stack;
            // every reported row in that quiver must remain equipped.
            auto &quiver = quivers[row->second.itemClass + '\n' + row->second.metadata.dump()];
            quiver.rows.emplace(id, worn);
            quiver.amount += worn;
            // Arrows, bolts and firearm ammunition have independent native
            // selections. A fuller arrow quiver must not displace the bolts
            // a crossbow can spend and subsequently recover.
            const auto *ammunition = KCDC::Shared::Game::FindMissileClass(KCDC::Shared::Game::ItemClassIdFromGuid(row->second.itemClass));
            auto &selection        = selected[ammunition != nullptr ? ammunition->ammunitionMask : 0];
            if (selection == nullptr || quiver.amount > selection->amount) {
                selection = &quiver;
            }
        }
        for (const auto &[type, quiver] : selected) {
            equipped.insert(quiver->rows.begin(), quiver->rows.end());
        }
        return equipped;
    }

    void InventoryGameplay::ClampEquipment(const Inventory &inventory, std::map<std::string, std::uint32_t> &equipped) {
        std::erase_if(equipped, [&inventory](auto &entry) {
            const auto row = inventory.Rows().find(entry.first);
            if (row == inventory.Rows().end()) {
                return true;
            }
            entry.second = std::min(entry.second, row->second.amount);
            return false;
        });
    }

    std::vector<PocketLine> InventoryGameplay::Pockets(const Inventory &victim, const std::map<std::string, std::uint32_t> &equipped) {
        std::vector<PocketLine> lines;
        for (const auto &[id, row] : victim.Rows()) {
            const std::uint32_t loose = row.amount - std::min(row.amount, Worn(equipped, id));
            if (loose == 0) {
                continue;
            }
            const auto line = std::find_if(lines.begin(), lines.end(), [&row](const PocketLine &existing) {
                return existing.itemClass == row.itemClass;
            });
            if (line == lines.end()) {
                lines.push_back({row.itemClass, loose});
            }
            else {
                line->amount += loose;
            }
        }
        return lines;
    }

    InventoryResult InventoryGameplay::Steal(Inventory &victim, Inventory &thief, const std::map<std::string, std::uint32_t> &equipped, const std::vector<PocketLine> &offer, const std::vector<PocketLine> &claim) {
        if (claim.empty()) {
            return {false, "invalidItems"};
        }
        std::vector<InventoryUnit> units;
        std::set<std::string> claimed;
        for (const auto &line : claim) {
            const auto offered = std::find_if(offer.begin(), offer.end(), [&line](const PocketLine &entry) {
                return entry.itemClass == line.itemClass;
            });
            if (line.amount == 0 || offered == offer.end() || line.amount > offered->amount || !claimed.insert(line.itemClass).second) {
                return {false, "notOffered"};
            }
            // Taken from the unworn units of the class, row by row.
            std::uint32_t remaining = line.amount;
            for (const auto &[id, row] : victim.Rows()) {
                if (remaining == 0) {
                    break;
                }
                if (row.itemClass != line.itemClass) {
                    continue;
                }
                const std::uint32_t loose = row.amount - std::min(row.amount, Worn(equipped, id));
                const std::uint32_t take  = std::min(loose, remaining);
                if (take == 0) {
                    continue;
                }
                units.push_back({id, take});
                remaining -= take;
            }
            if (remaining != 0) {
                return {false, "insufficientItems"};
            }
        }
        return Inventory::Transfer(victim, thief, units, std::nullopt, std::nullopt, "pickpocket");
    }

    std::string InventoryGameplay::TheftRefusal(std::uint64_t offeredAtMs, std::uint64_t nowMs, const glm::vec3 &thief, std::uint32_t thiefWorld, const glm::vec3 &victim, std::uint32_t victimWorld) {
        if (nowMs < offeredAtMs + kMinimumTheftMs) {
            return "tooEarly";
        }
        if (thiefWorld != victimWorld) {
            return "otherWorld";
        }
        if (glm::distance(thief, victim) > kMaximumTheftReach) {
            return "tooFar";
        }
        return {};
    }

    std::vector<InventoryUnit> InventoryGameplay::DropUnits(const Inventory &inventory, const std::map<std::string, std::uint32_t> &equipped, bool keepEquipped, std::size_t maximumRows) {
        std::vector<InventoryUnit> units;
        for (const auto &[id, row] : inventory.Rows()) {
            if (units.size() >= maximumRows) {
                break;
            }
            const std::uint32_t amount = keepEquipped ? row.amount - std::min(row.amount, Worn(equipped, id)) : row.amount;
            if (amount != 0) {
                units.push_back({id, amount});
            }
        }
        return units;
    }

    bool SpentRounds::WithinReach(const glm::vec3 &shooter, const glm::vec3 &landing) {
        return glm::distance(shooter, landing) <= kMaximumRecoveryRange;
    }

    bool SpentRounds::LandsAs(const std::string &fired, const std::string &landed) {
        if (landed == fired) {
            return true;
        }
        const auto *round  = KCDC::Shared::Game::FindItemClass(fired);
        const auto *broken = KCDC::Shared::Game::FindItemClass(landed);
        if (round == nullptr || broken == nullptr || round->category != "Ammo") {
            return false;
        }
        // The broken round of the same kind. Shipped ammunition names its kind;
        // the Czech sipka is an arrow.
        std::string name(round->name);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        std::string_view kind;
        if (name.find("bolt") != std::string::npos) {
            kind = "bolt_broken";
        }
        else if (name.find("arrow") != std::string::npos || name.find("sipka") != std::string::npos) {
            kind = "arrow_broken";
        }
        else if (name.find("scatter") != std::string::npos || name.find("shotgun") != std::string::npos) {
            kind = "pellet_broken";
        }
        else if (name.find("ball") != std::string::npos || name.find("shot") != std::string::npos) {
            kind = "ball_broken";
        }
        return !kind.empty() && broken->name == kind;
    }

    void SpentRounds::Fire(const InventoryRow &round, std::uint32_t amount, std::uint32_t virtualWorld, std::uint64_t nowMs) {
        Expire(nowMs);
        for (std::uint32_t i = 0; i < amount; ++i) {
            if (_rounds.size() >= kMaximum) {
                _rounds.pop_front();
            }
            _rounds.push_back({round.itemClass, round.metadata, nowMs, virtualWorld});
        }
    }

    const SpentRound *SpentRounds::Find(const std::string &itemClass, std::uint32_t virtualWorld, std::uint64_t nowMs) {
        Expire(nowMs);
        const auto found = std::find_if(_rounds.begin(), _rounds.end(), [&](const SpentRound &round) {
            return round.itemClass == itemClass && round.virtualWorld == virtualWorld;
        });
        return found == _rounds.end() ? nullptr : &*found;
    }

    std::optional<SpentRound> SpentRounds::Claim(const std::string &itemClass, std::uint32_t virtualWorld, std::uint64_t nowMs) {
        Expire(nowMs);
        const auto found = std::find_if(_rounds.begin(), _rounds.end(), [&](const SpentRound &round) {
            return round.itemClass == itemClass && round.virtualWorld == virtualWorld;
        });
        if (found == _rounds.end()) {
            return std::nullopt;
        }
        SpentRound round = std::move(*found);
        _rounds.erase(found);
        return round;
    }

    void SpentRounds::Expire(std::uint64_t nowMs) {
        while (!_rounds.empty() && nowMs >= _rounds.front().firedAtMs + kLifetimeMs) {
            _rounds.pop_front();
        }
    }
} // namespace KCDC::Features::Inventory
