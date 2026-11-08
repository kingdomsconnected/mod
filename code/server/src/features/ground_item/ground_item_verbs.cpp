#include "ground_item_verbs.h"
#include "features/player/player_roster.h"

#include "core/server.h"

#include "features/ground_item/ground_item_scripting.h"
#include "features/ground_item/ground_stock.h"

#include "shared/entity_filter.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace KCDC::Features::GroundItem {
    namespace {
        constexpr const char *kLogger = "GroundItem";

        // A class as the 32 hex digits every other surface spells it with -- the wire, `groundItem.itemClass`, `player.equipment`. The server holds no name for one: naming a class is the item database's job, and a stack a client dropped may carry a class this build has never
        // heard of.
        std::string ClassText(const Shared::RPC::ItemClassId &itemClass) {
            return fmt::format("{:016x}{:016x}", itemClass.low, itemClass.high);
        }
    } // namespace

    Shared::Entities::GroundItemEntity *GroundItems::Spawn(Shared::RPC::InventoryItem item, const glm::vec3 &position, const glm::quat &rotation, std::uint32_t virtualWorld, bool carryable, const std::string &name) {
        if (!Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation)) {
            return nullptr;
        }

        // The receiver builds the stack from the class and these scalars
        // alone, so they are the whole description; the identity is ours to
        // mint because no inventory reported this one.
        item.equipped      = false;
        item.equipmentSlot = Shared::RPC::kNoEquipmentSlot;
        item.amount        = std::clamp(item.amount, 1U, Shared::RPC::kMaxGiveAmount);
        GroundStock::ApplyClassDefaults(item);
        if (item.inventoryItemId == 0) {
            auto *server = Server::Get();
            if (server == nullptr) {
                return nullptr;
            }
            item.inventoryItemId = server->GetGroundItems().NextServerItemId();
        }

        if (!GroundStock::CanPlace(item)) {
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *groundItem = replication->CreateEntity<Shared::Entities::GroundItemEntity>();
        if (groundItem == nullptr) {
            return nullptr;
        }

        const glm::quat normalized = glm::normalize(rotation);

        // All of it before the entity replicates, so it rides the construction
        // snapshot rather than arriving a tick after the receiver built the
        // stack from the defaults.
        groundItem->item      = item;
        groundItem->position  = position;
        groundItem->rotation  = normalized;
        groundItem->carryable = carryable;
        groundItem->name      = name;

        // Born settled: there is no dropper to sample physics and publish a
        // final pose, so this stack has one from the start. Both poses are the
        // same one because the receiver spawns the pickable at `position` and
        // only afterwards corrects it to the resting pose.
        groundItem->restPosition = position;
        groundItem->restRotation = normalized;
        groundItem->velocity     = glm::vec3(0.0F);
        groundItem->resting      = true;

        // Resting from the start, so no client ever simulates it: it stays
        // exactly where it was put.
        groundItem->streaming.range = Shared::Entities::GroundItemEntity::kStreamRange;
        groundItem->SetVirtualWorld(virtualWorld);

        Framework::Logging::GetLogger(kLogger)->info("Spawned ground item {} ('{}' x{}) in world {} at {:.2f}, {:.2f}, {:.2f}", groundItem->GetNetworkID(), ClassText(item.itemClass), item.amount, virtualWorld, position.x, position.y, position.z);

        // A handler can take the stack straight back: hand on only one that is still there.
        const std::uint64_t networkId = groundItem->GetNetworkID();
        Scripting::Events::GroundItemSpawned(networkId);
        return replication->GetEntity<Shared::Entities::GroundItemEntity>(networkId);
    }

    Shared::Entities::GroundItemEntity *GroundItems::Drop(Shared::RPC::InventoryItem item, Shared::Entities::HumanEntity &source, bool retiresHeldItem, bool carryable) {
        item.equipped      = false;
        item.equipmentSlot = Shared::RPC::kNoEquipmentSlot;
        GroundStock::ApplyClassDefaults(item);
        auto *server = Server::Get();
        if (server == nullptr || !GroundStock::CanPlace(item)) {
            return nullptr;
        }
        item.inventoryItemId = server->GetGroundItems().NextServerItemId();

        auto *replication = Framework::CoreModules::GetReplication();
        auto *groundItem  = replication != nullptr ? replication->CreateEntity<Shared::Entities::GroundItemEntity>() : nullptr;
        if (groundItem == nullptr) {
            return nullptr;
        }

        // Where the game puts a dropped item: in front of the player, falling.
        // The nearest client simulates the fall; the server takes it back once
        // it rests (GroundItemService::Update).
        const glm::quat facing            = glm::normalize(source.rotation);
        groundItem->item                  = item;
        groundItem->sourcePlayerNetworkId = source.GetNetworkID();
        groundItem->retiresHeldItem       = retiresHeldItem;
        groundItem->carryable             = carryable;
        groundItem->position              = source.position + facing * glm::vec3(0.0F, 0.7F, 0.25F);
        groundItem->rotation              = facing;
        groundItem->resting               = false;
        groundItem->streaming.range       = Shared::Entities::GroundItemEntity::kStreamRange;
        groundItem->SetVirtualWorld(source.GetVirtualWorld());

        Scripting::Events::GroundItemSpawned(groundItem->GetNetworkID());
        return groundItem;
    }

    Shared::Entities::GroundItemEntity *GroundItems::Place(const Shared::Game::ItemPlacementDefinition &placement, std::uint32_t virtualWorld, std::string &error) {
        // The same resolver `/give`, `player.giveItem` and `GroundItem.spawn` take, so one spelling of an item works everywhere.
        const Shared::Game::ItemClassEntry *itemClass = KCDC::Features::Player::Players::ResolveItemClass(placement.item);
        if (itemClass == nullptr) {
            error = "'" + placement.item + "' is not an item class in the game's tables";
            return nullptr;
        }
        Shared::RPC::InventoryItem item;
        item.itemClass                                 = Shared::Game::ItemClassIdFromGuid(itemClass->guid);
        item.amount                                    = placement.amount;
        item.quality                                   = placement.quality;
        item.health                                    = placement.health;
        item.condition                                 = placement.condition;
        Shared::Entities::GroundItemEntity *groundItem = Spawn(item, placement.position, placement.rotation, virtualWorld, false, placement.name);
        if (groundItem == nullptr) {
            error = "no client could build " + placement.item + " as described, or a groundItemSpawn handler took it away";
        }
        return groundItem;
    }

    void GroundItems::Destroy(Shared::Entities::GroundItemEntity *groundItem) {
        if (groundItem == nullptr) {
            return;
        }

        // While the entity is still here, so a handler can read it.
        Scripting::Events::GroundItemDestroyed(groundItem->GetNetworkID());
        if (auto *server = Server::Get()) {
            server->GetGroundItems().Forget(groundItem->GetNetworkID());
        }

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(groundItem);
        }
    }

    std::vector<Shared::Entities::GroundItemEntity *> GroundItems::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Shared::Entities::GroundItemEntity *> groundItems;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::GroundItemEntity>([&](Shared::Entities::GroundItemEntity *groundItem) {
                if (Shared::Entities::InWorld(groundItem, virtualWorld)) {
                    groundItems.push_back(groundItem);
                }
            });
        }
        return groundItems;
    }

    std::size_t GroundItems::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        // Collected first: destroying while iterating would mutate the set the walk is standing in.
        const std::vector<Shared::Entities::GroundItemEntity *> doomed = Collect(virtualWorld);
        for (auto *groundItem : doomed) {
            Destroy(groundItem);
        }

        if (!doomed.empty()) {
            Framework::Logging::GetLogger(kLogger)->info("Despawned {} ground item(s)", doomed.size());
        }
        return doomed.size();
    }

    std::size_t GroundItems::Count(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::size_t count = 0;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::GroundItemEntity>([&](Shared::Entities::GroundItemEntity *groundItem) {
                count += Shared::Entities::InWorld(groundItem, virtualWorld) ? 1 : 0;
            });
        }
        return count;
    }

    std::vector<std::string> GroundItems::Describe(std::optional<MafiaNet::VirtualWorldId> virtualWorld, std::size_t limit) {
        std::vector<Shared::Entities::GroundItemEntity *> groundItems = Collect(virtualWorld);

        // Newest first, and only here: NetworkIDs are handed out in order, so
        // the id is the age, and nothing but a listing cares about the order.
        std::sort(groundItems.begin(), groundItems.end(), [](Shared::Entities::GroundItemEntity *lhs, Shared::Entities::GroundItemEntity *rhs) {
            return lhs->GetNetworkID() > rhs->GetNetworkID();
        });

        std::vector<std::string> lines;
        for (auto *groundItem : groundItems) {
            if (lines.size() >= limit) {
                break;
            }
            lines.push_back(
                fmt::format("{}: {} x{} at {:.1f}, {:.1f}, {:.1f}{}", groundItem->GetNetworkID(), ClassText(groundItem->item.itemClass), groundItem->item.amount, groundItem->position.x, groundItem->position.y, groundItem->position.z, groundItem->resting ? "" : " (falling)"));
        }
        return lines;
    }
} // namespace KCDC::Features::GroundItem
