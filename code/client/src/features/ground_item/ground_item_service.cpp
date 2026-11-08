#include "ground_item_service.h"
#include "features/items/custom_item_module.h"

#include "core/application.h"
#include "features/player/player_inventory_service.h"
#include "features/player/player_service.h"
#include "features/ui/ui_action_hints.h"
#include "common/module_support.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/system/i_localization_manager.h"
#include "sdk/wh/entitymodule/c_entity_module.h"
#include "sdk/wh/entitymodule/c_inventory_manager.h"
#include "sdk/wh/entitymodule/c_item_attachment_manager.h"
#include "sdk/wh/entitymodule/c_item_manager.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"
#include "shared/features/ground_item/ground_item_inventory_message.h"
#include "shared/features/player/item_carry_policy.h"
#include "shared/features/player/player_carry.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_set>

namespace KCDC::Features::GroundItem {
    namespace {
        /** A claim the server never answered is given up after this. */
        constexpr std::uint64_t kPickupTimeoutMs = 15000;

        /** How long one ask to carry a prop holds before the prompt may ask again. */
        constexpr std::uint64_t kCarryAskIntervalMs = 1000;

        /** How close a prop has to be for the prompt: an arm's length, inside the server's own reach. */
        constexpr float kCarryPromptReach = 2.0F;

        /** How often the nearest prop is looked for; a prop does not wander off between. */
        constexpr std::uint64_t kCarryPromptIntervalMs = 200;

        /** The game's own "Pick up", in the player's language, or English when the table has none. */
        const std::string &PickUpText() {
            static const std::string text = [] {
                SDK::ILocalizationManager *strings = SDK::ILocalizationManager::GetInstance();
                SDK::ScopedCryString localized("");
                return strings != nullptr && strings->LocalizeLabel("@ui_pickup_item", *localized.Out()) && localized.c_str()[0] != '\0' ? std::string(localized.c_str()) : std::string("Pick up");
            }();
            return text;
        }

        std::uint64_t NowMilliseconds() {
            return MafiaNet::GetTimeUS() / 1000;
        }
    } // namespace

    SDK::wh::entitymodule::S_ItemClassGUID ToNativeItemClass(const Shared::RPC::ItemClassId &itemClass) {
        return KCDC::Features::Items::CustomItemModule::Get().ToNative(itemClass);
    }

    bool SameItemClass(const Shared::RPC::ItemClassId &lhs, const SDK::wh::entitymodule::S_ItemClassGUID &rhs) {
        const auto expected = ToNativeItemClass(lhs);
        return expected.m_low == rhs.m_low && expected.m_high == rhs.m_high;
    }

    SDK::wh::entitymodule::C_Inventory *InventoryOf(SDK::wh::entitymodule::C_Human *human) {
        auto *runtime = human != nullptr ? human->GetInventorySoulRuntime() : nullptr;
        return runtime != nullptr ? runtime->GetInventory() : nullptr;
    }

    SDK::wh::entitymodule::C_WorldInventory *WorldInventory() {
        auto *manager = SDK::wh::entitymodule::C_InventoryManager::GetInstance();
        return manager != nullptr ? manager->GetWorldInventory() : nullptr;
    }

    SDK::wh::entitymodule::C_Item *FindItem(std::uint64_t itemWuid) {
        SDK::wh::entitymodule::C_EntityModule *entityModule = SDK::wh::entitymodule::C_EntityModule::GetInstance();
        return entityModule != nullptr && itemWuid != 0 ? entityModule->GetItemManager()->FindItem(SDK::wh::framework::WUID {itemWuid}) : nullptr;
    }

    std::vector<SDK::wh::entitymodule::C_Item *> CreateWorldItems(const Shared::RPC::ItemClassId &itemClass, std::uint32_t amount, std::uint32_t quality, float health, float condition) {
        std::vector<SDK::wh::entitymodule::C_Item *> created;
        auto *world = WorldInventory();
        if (world == nullptr || !itemClass.IsValid() || amount == 0) {
            return created;
        }

        std::unordered_set<std::uint64_t> before;
        before.reserve(world->GetOwnedItemCount());
        for (std::size_t i = 0; i < world->GetOwnedItemCount(); ++i) {
            if (SDK::wh::entitymodule::C_Item *owned = world->GetOwnedItemAt(i)) {
                before.insert(owned->GetId());
            }
        }
        {
            // Not the player's stock: the inventory authority stands aside.
            KCDC::Features::Player::PlayerInventoryService::AuthorityProjection applying(KCDC::Core::gApplication->GetPlayerInventoryService());
            world->CreateItems(ToNativeItemClass(itemClass), static_cast<int>(amount), false, quality, health, condition);
        }
        for (std::size_t i = 0; i < world->GetOwnedItemCount(); ++i) {
            SDK::wh::entitymodule::C_Item *candidate = world->GetOwnedItemAt(i);
            if (candidate != nullptr && !before.contains(candidate->GetId()) && SameItemClass(itemClass, candidate->InvokeGetItemClassRaw())) {
                created.push_back(candidate);
            }
        }
        return created;
    }

    void GroundItemService::Install() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::RPC::GroundItemPickupResult>([](const Shared::RPC::GroundItemPickupResult &result, MafiaNet::Packet *) {
            if (KCDC::Core::gApplication == nullptr || !result.IsSane()) {
                return;
            }
            auto &service = KCDC::Core::gApplication->GetGroundItemService();
            std::scoped_lock lock(service._incomingMutex);
            if (service._incoming.size() < 64) {
                service._incoming.push_back(result);
            }
        });
    }

    void GroundItemService::Update() {
        std::vector<Shared::RPC::GroundItemPickupResult> incoming;
        {
            std::scoped_lock lock(_incomingMutex);
            incoming.swap(_incoming);
        }
        for (const auto &result : incoming) {
            HandlePickupResult(result);
        }
        if (_pendingPickup.has_value() && _pendingPickup->expiresAt <= NowMilliseconds()) {
            _pendingPickup.reset();
        }
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<GroundItem>([](GroundItem *item) {
                item->Frame();
                item->EnsureMaterialized();
            });
        }
        if (NowMilliseconds() >= _nextCarryPromptCheck) {
            _nextCarryPromptCheck = NowMilliseconds() + kCarryPromptIntervalMs;
            UpdateCarryPrompt();
        }
    }

    void GroundItemService::UpdateCarryPrompt() {
        auto *actor                                         = KCDC::Common::LocalHumanActor();
        SDK::IEntity *entity                                = actor != nullptr ? actor->GetEntity() : nullptr;
        SDK::wh::entitymodule::C_ItemAttachmentManager *hands = actor != nullptr ? actor->GetItemAttachmentManager() : nullptr;
        auto *replication                                   = Framework::CoreModules::GetReplication();
        SDK::wh::entitymodule::C_Player *player = SDK::wh::entitymodule::C_Player::GetInstance();
        const SDK::EntityId offered             = player != nullptr ? player->GetInteractor()->GetTargetEntityId() : 0;
        // The server's own rule, so the prompt never offers what it would refuse.
        const bool free = entity != nullptr && hands != nullptr && replication != nullptr && hands->InvokeAreHandsEmpty() && Shared::Game::ItemCarryRefusal(KCDC::Core::gApplication->GetPlayerService().LocalItemCarryState()) == nullptr;

        GroundItem *nearest = nullptr;
        // The game's own "Pick up" on a stack the player looks at takes the
        // same key: on a prop it is already a carry (RequestCarryInstead), on
        // any other stack it would fire beside this prompt.
        bool gameOffersPickUp = false;
        if (free) {
            const glm::vec3 from = KCDC::Common::ToGlm(entity->GetWorldPos());
            float best           = kCarryPromptReach;
            replication->ForEach<GroundItem>([&](GroundItem *prop) {
                if (prop->GetNativeItemWuid() == 0) {
                    return;
                }
                if (offered != 0 && prop->IsPickableEntity(offered)) {
                    gameOffersPickUp = true;
                }
                if (!prop->carryable) {
                    return;
                }
                const float distance = glm::distance(from, prop->resting ? prop->restPosition : prop->position);
                if (distance <= best) {
                    best    = distance;
                    nearest = prop;
                }
            });
        }

        const std::uint64_t propId = nearest != nullptr && !gameOffersPickUp ? nearest->GetNetworkID() : 0;
        if (propId == _carryPromptProp && (_carryPrompt != 0) == (propId != 0)) {
            return;
        }
        HideCarryPrompt();
        if (propId == 0) {
            return;
        }

        KCDC::Features::UI::ActionHints::Options options;
        options.key      = "primary_action";
        options.text     = PickUpText();
        _carryPrompt     = KCDC::Features::UI::ActionHints::Get().Create({}, options, [propId](std::uint32_t) {
            auto *prop = KCDC::Common::FindReplicaById<GroundItem>(propId);
            if (prop != nullptr && KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetGroundItemService().RequestCarry(*prop);
            }
        });
        _carryPromptProp = _carryPrompt != 0 ? propId : 0;
    }

    void GroundItemService::HideCarryPrompt() {
        if (_carryPrompt != 0) {
            KCDC::Features::UI::ActionHints::Get().Remove(_carryPrompt);
        }
        _carryPrompt     = 0;
        _carryPromptProp = 0;
    }

    void GroundItemService::Reset() {
        _dropSelection = {};
        _pendingPickup.reset();
        _nextPickupToken = 1;
        _carryAskedFor   = 0;
        _carryAskedUntil = 0;
        // ActionHints takes every hint down with the session itself.
        _carryPrompt          = 0;
        _carryPromptProp      = 0;
        _nextCarryPromptCheck = 0;
        std::scoped_lock lock(_incomingMutex);
        _incoming.clear();
    }

    bool GroundItemService::RequestDrop(std::uint64_t wuid, std::uint32_t amount, bool held) {
        auto &inventory = KCDC::Core::gApplication->GetPlayerInventoryService();
        if (!inventory.HasAuthority()) {
            return false;
        }
        // The native drop never runs: what leaves the bags is what the
        // server removes, and its sync takes the native stack away.
        if (!inventory.IsSynced()) {
            _dropSelection.Refuse();
            return true;
        }
        const bool standalone = !_dropSelection.IsActive();
        if (standalone) {
            BeginDropSelection();
        }
        if (amount == 0) {
            auto *nativeInventory = InventoryOf(KCDC::Common::LocalHumanActor());
            auto *nativeItem      = nativeInventory != nullptr ? nativeInventory->FindOwnedItemById(wuid) : nullptr;
            amount                = nativeItem != nullptr ? nativeItem->GetAmount() : 0;
        }
        if (amount == 0) {
            _dropSelection.Refuse();
        }
        for (const auto &unit : inventory.UnitsOf(wuid)) {
            if (amount == 0) {
                break;
            }
            const auto take = std::min(amount, unit.amount);
            if (take != 0) {
                _dropSelection.Add(unit.id, take, held);
            }
            amount -= take;
        }
        if (amount != 0) {
            _dropSelection.Refuse();
        }
        if (standalone) {
            FinishDropSelection();
        }
        return true;
    }

    void GroundItemService::BeginDropSelection() {
        _dropSelection.Begin();
    }

    void GroundItemService::FinishDropSelection() {
        const auto body = _dropSelection.Finish();
        if (_dropSelection.IsActive()) {
            return;
        }
        if (!_dropSelection.IsValid()) {
            KCDC::Core::gApplication->GetPlayerInventoryService().RefuseUnsupportedAction("The drop selection is too large or has changed. Select fewer items or reopen the inventory.");
            return;
        }
        if (!body) {
            return;
        }
        Shared::RPC::GroundItemDropRequest request;
        request.body = *body;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    void GroundItemService::OnLocalDrop(SDK::wh::entitymodule::C_Item *, bool) {
        // Nothing reaches the world natively; the next projection puts back
        // whatever such a path took out of the bags.
        KCDC::Core::gApplication->GetPlayerInventoryService().NotifyNativeChange();
    }

    bool GroundItemService::OnPickupStarting(SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor, bool) {
        if (descriptor == nullptr || _pendingPickup.has_value() || !KCDC::Core::gApplication->GetPlayerInventoryService().IsSynced()) {
            return false;
        }
        const auto wuid = descriptor->GetItemWuid();
        auto *ground    = KCDC::Common::FindReplica<GroundItem>([wuid](GroundItem *candidate) {
            return candidate->GetNativeItemWuid() == wuid;
        });
        if (ground == nullptr) {
            return false;
        }
        Shared::RPC::GroundItemPickupStarted request;
        request.groundItemNetworkId = ground->GetNetworkID();
        request.pickupToken         = _nextPickupToken++;
        _pendingPickup              = PendingPickup {request.groundItemNetworkId, request.pickupToken, wuid, NowMilliseconds() + kPickupTimeoutMs};
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        return true;
    }

    GroundItem *GroundItemService::FindPropByNativeItem(std::uint64_t itemWuid) {
        return itemWuid != 0 ? KCDC::Common::FindReplica<GroundItem>([itemWuid](GroundItem *candidate) { return candidate->GetNativeItemWuid() == itemWuid; }) : nullptr;
    }

    bool GroundItemService::RequestCarryInstead(const SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor) {
        auto *prop = FindPropByNativeItem(descriptor != nullptr ? descriptor->GetItemWuid() : 0);
        if (prop == nullptr || !prop->carryable) {
            return false;
        }
        RequestCarry(*prop);
        return true;
    }

    void GroundItemService::RequestCarry(GroundItem &prop) {
        // A prompt fires again while the answer is in flight; one ask per prop.
        const std::uint64_t now = NowMilliseconds();
        if (_carryAskedFor == prop.GetNetworkID() && now < _carryAskedUntil) {
            return;
        }
        _carryAskedFor   = prop.GetNetworkID();
        _carryAskedUntil = now + kCarryAskIntervalMs;

        Shared::RPC::RequestCarryGroundItem request;
        request.groundItemNetworkId = prop.GetNetworkID();
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(request, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }

    bool GroundItemService::IsPickupTrackedFor(const SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor) const {
        return descriptor != nullptr && _pendingPickup.has_value() && _pendingPickup->wuid == descriptor->GetItemWuid();
    }

    void GroundItemService::OnPickupSettled(bool) {}

    void GroundItemService::OnPickupCancelled() {
        _pendingPickup.reset();
    }

    bool GroundItemService::HasPendingHeldItemDrop(std::uint64_t sourcePlayerNetworkId, const Shared::RPC::ItemClassId &heldClass) {
        return sourcePlayerNetworkId != 0 && KCDC::Common::FindReplica<GroundItem>([sourcePlayerNetworkId, &heldClass](GroundItem *item) {
            return item->IsAwaitingHeldShadow(sourcePlayerNetworkId, heldClass);
        }) != nullptr;
    }

    void GroundItemService::HandlePickupResult(const Shared::RPC::GroundItemPickupResult &result) {
        if (!_pendingPickup.has_value() || _pendingPickup->networkId != result.groundItemNetworkId || _pendingPickup->token != result.pickupToken) {
            return;
        }
        if (result.disposition != Shared::RPC::GroundItemPickupDisposition::Reserved) {
            _pendingPickup.reset();
            return;
        }
        // No native pickup runs while the server holds the inventory, so the
        // reservation is taken at once; the award arrives as an inventory sync.
        Shared::RPC::GroundItemPickupCompleted completed;
        completed.groundItemNetworkId = result.groundItemNetworkId;
        completed.pickupToken         = result.pickupToken;
        completed.succeeded           = true;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(completed, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
    }
} // namespace KCDC::Features::GroundItem
