#include "ground_stock.h"

#include "shared/game/cry_guid_text.h"
#include "shared/game/custom_item_registry.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/game/item_classes.h"
#include "shared/game/missile_catalog.h"
#include "shared/inventory_item_properties.h"
#include "shared/pose_math.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <set>

namespace KCDC::Features::GroundItem {
    bool GroundStock::ReadDropRequest(const std::string &body, std::vector<KCDC::Shared::Inventory::DropUnit> &units) {
        const auto request = nlohmann::json::parse(body, nullptr, false);
        if (!request.is_object() || !request.contains("units") || !request.at("units").is_array() || request.at("units").empty()) {
            return false;
        }
        if (request.at("units").size() > kMaximumDropUnits) {
            return false;
        }
        if (request.contains("held") && !request.at("held").is_boolean()) {
            return false;
        }
        std::vector<KCDC::Shared::Inventory::DropUnit> parsed;
        std::set<std::string> seen;
        for (const auto &unit : request.at("units")) {
            if (!unit.is_object() || !unit.contains("id") || !unit.at("id").is_string() || !unit.contains("amount") || !unit.at("amount").is_number_unsigned()) {
                return false;
            }
            const auto amount = unit.at("amount").get<std::uint64_t>();
            const auto &id    = unit.at("id").get_ref<const std::string &>();
            if (id.empty() || id.size() > 64 || amount == 0 || amount > KCDC::Features::Inventory::Inventory::kMaximumAmount || !seen.insert(id).second) {
                return false;
            }
            if (unit.contains("held") && !unit.at("held").is_boolean()) {
                return false;
            }
            // Old clients marked the entire request. It could retire a hand
            // only when that request named exactly one row.
            const bool held = unit.value("held", request.at("units").size() == 1 && request.value("held", false));
            parsed.push_back({id, static_cast<std::uint32_t>(amount), held});
        }
        units = std::move(parsed);
        return true;
    }

    GroundStockDrop GroundStock::Drop(KCDC::Features::Inventory::Inventory &inventory, const std::vector<KCDC::Shared::Inventory::DropUnit> &units, std::size_t availableStacks) {
        GroundStockDrop drop;
        availableStacks = std::min(availableStacks, kMaximumDropUnits);
        std::vector<GroundStockStack> stacks;
        std::vector<KCDC::Features::Inventory::InventoryUnit> removal;
        for (const auto &unit : units) {
            removal.push_back({unit.id, unit.amount});
            const auto row = inventory.Rows().find(unit.id);
            if (row == inventory.Rows().end()) {
                continue;
            }
            // The stack's own identity is minted when it is put down.
            Shared::RPC::InventoryItem stack;
            stack.inventoryItemId = 1;
            stack.itemClass       = Shared::Game::ItemClassIdFromGuid(row->second.itemClass);
            stack.amount          = unit.amount;
            if (!Shared::Inventory::ApplyItemProperties(row->second.metadata, stack)) {
                drop.result = {false, "invalidItem"};
                return drop;
            }
            stack.customStackKey = Shared::Game::CustomItemRegistry::Get().PublicMetadata(row->second.metadata).value("customStackKey", std::string());
            stack.equipped      = false;
            stack.equipmentSlot = Shared::RPC::kNoEquipmentSlot;
            ApplyClassDefaults(stack);
            const auto *definition = Shared::Game::FindInventoryPropertyClass(row->second.itemClass);
            const bool stackable = definition != nullptr && definition->nativeStackable;
            const auto count = stackable ? 1U : stack.amount;
            if (count > availableStacks - stacks.size()) {
                drop.result = {false, "groundCapacity"};
                return drop;
            }
            if (!stackable) {
                stack.amount = 1;
            }
            // Checked before anything leaves the inventory: a stack no
            // receiver could build would be units taken and never put down.
            if (!CanPlace(stack)) {
                drop.result = {false, "cannotPlace"};
                return drop;
            }
            // Only one physical item can inherit the source's held shadow.
            const bool retiresHeld = unit.held && (!stackable || row->second.amount == unit.amount);
            for (std::uint32_t index = 0; index < count; ++index) {
                stacks.push_back({stack, retiresHeld && index == 0});
            }
        }
        drop.result = inventory.Remove(removal, std::nullopt, "drop");
        if (drop.result.ok) {
            drop.stacks = std::move(stacks);
        }
        return drop;
    }

    KCDC::Features::Inventory::InventoryResult GroundStock::Pickup(KCDC::Features::Inventory::Inventory &inventory, const Shared::RPC::InventoryItem &stack, const char *reason, bool notify) {
        auto metadata = Shared::Inventory::EncodeItemProperties(stack);
        metadata.erase("customStackKey");
        return inventory.Add(Shared::Game::CryGuidToString(stack.itemClass.low, stack.itemClass.high), stack.amount, metadata, std::nullopt, reason, notify);
    }

    std::string GroundStock::PickupRefusal(const glm::vec3 &claimant, std::uint32_t claimantWorld, bool canAct, const glm::vec3 &stack, std::uint32_t stackWorld, bool resting) {
        if (claimantWorld != stackWorld) {
            return "otherWorld";
        }
        // A falling stack's pose is its simulator's word, and reach checked
        // against it would be reach the simulator chose.
        if (!resting) {
            return "falling";
        }
        if (!canAct) {
            return "cannotAct";
        }
        if (glm::distance(claimant, stack) > kPickupRange) {
            return "tooFar";
        }
        return {};
    }

    glm::vec3 GroundStock::SettledPosition(const glm::vec3 &origin, const glm::vec3 &reported) {
        return Shared::Game::IsFinite(reported) && glm::distance(origin, reported) <= kMaximumFallDistance ? reported : origin;
    }

    void GroundStock::ApplyClassDefaults(Shared::RPC::InventoryItem &item) {
        if (Shared::Game::FindMissileClass(item.itemClass) == nullptr) {
            return;
        }
        // `MissileItemProperties::IsSane` wants a real quality and an absolute
        // health, where the rest of the inventory wire uses 0 and -1 to mean
        // "whatever the class says".
        item.quality = std::max<std::uint32_t>(item.quality, 1U);
        if (!(item.health >= 0.0F)) {
            item.health = 1.0F;
        }
    }

    bool GroundStock::CanPlace(const Shared::RPC::InventoryItem &item) {
        const auto *definition = Shared::Game::FindInventoryPropertyClass(Shared::Game::CryGuidToString(item.itemClass.low, item.itemClass.high));
        return item.IsSane() && Shared::Game::IsValidMissileGroundItem(item) && definition != nullptr && !definition->questItem && (item.amount == 1 || definition->nativeStackable);
    }

    bool GroundCarryClaims::IsClaimable(std::uint64_t stack, std::uint64_t carrier, std::uint64_t now) const {
        if (_retiring.contains(stack)) {
            return false;
        }
        const auto claim = _claims.find(stack);
        return claim == _claims.end() || claim->second.carrier == carrier || claim->second.expiresAt <= now;
    }

    bool GroundCarryClaims::Claim(std::uint64_t stack, std::uint64_t carrier, std::uint64_t now) {
        if (carrier == 0 || !IsClaimable(stack, carrier, now)) {
            return false;
        }
        _claims[stack] = {carrier, now + kClaimTimeoutMs};
        return true;
    }

    bool GroundCarryClaims::Holds(std::uint64_t stack, std::uint64_t carrier, std::uint64_t now) const {
        const auto claim = _claims.find(stack);
        return carrier != 0 && claim != _claims.end() && claim->second.carrier == carrier && claim->second.expiresAt > now;
    }

    bool GroundCarryClaims::IsHeld(std::uint64_t stack, std::uint64_t now) const {
        const auto claim = _claims.find(stack);
        return _retiring.contains(stack) || (claim != _claims.end() && claim->second.expiresAt > now);
    }

    void GroundCarryClaims::Release(std::uint64_t stack) {
        _claims.erase(stack);
    }

    void GroundCarryClaims::Retire(std::uint64_t stack, std::uint64_t now) {
        _claims.erase(stack);
        _retiring[stack] = now + kRetireDelayMs;
    }

    std::vector<std::uint64_t> GroundCarryClaims::TakeDue(std::uint64_t now) {
        std::erase_if(_claims, [now](const auto &claim) { return claim.second.expiresAt <= now; });
        std::vector<std::uint64_t> due;
        for (auto retiring = _retiring.begin(); retiring != _retiring.end();) {
            if (retiring->second <= now) {
                due.push_back(retiring->first);
                retiring = _retiring.erase(retiring);
            }
            else {
                ++retiring;
            }
        }
        return due;
    }

    void GroundCarryClaims::Forget(std::uint64_t stack) {
        _claims.erase(stack);
        _retiring.erase(stack);
    }

    bool GroundStockCounts::CanAdd(std::uint64_t source, std::size_t count) const {
        return Count(source) + count <= kMaximumPerPlayer;
    }

    void GroundStockCounts::Add(std::uint64_t stack, std::uint64_t source) {
        if (_sources.emplace(stack, source).second) {
            ++_counts[source];
        }
    }

    void GroundStockCounts::Remove(std::uint64_t stack) {
        const auto found = _sources.find(stack);
        if (found == _sources.end()) {
            return;
        }
        const auto count = _counts.find(found->second);
        if (count != _counts.end() && --count->second == 0) {
            _counts.erase(count);
        }
        _sources.erase(found);
    }

    std::uint32_t GroundStockCounts::Count(std::uint64_t source) const {
        const auto found = _counts.find(source);
        return found != _counts.end() ? found->second : 0;
    }
} // namespace KCDC::Features::GroundItem
