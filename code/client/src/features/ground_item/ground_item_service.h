#pragma once

#include "core/feature_registry.h"

#include "ground_item.h"

#include "shared/features/ground_item/ground_item_rpc.h"
#include "shared/features/ground_item/drop_selection.h"
#include "shared/item_stack.h"

#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/s_item_class_guid.h"
#include "sdk/wh/entitymodule/s_item_transfer_descriptor.h"
#include "sdk/wh/entitymodule/c_world_inventory.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace KCDC::Features::GroundItem {
    // --- Conversions between the wire's item class and the engine's --------

    SDK::wh::entitymodule::S_ItemClassGUID ToNativeItemClass(const Shared::RPC::ItemClassId &itemClass);
    bool SameItemClass(const Shared::RPC::ItemClassId &lhs, const SDK::wh::entitymodule::S_ItemClassGUID &rhs);

    /** The carried inventory behind a human's soul runtime, or nullptr. */
    SDK::wh::entitymodule::C_Inventory *InventoryOf(SDK::wh::entitymodule::C_Human *human);

    // --- Items lying in the world ---------------------------------------------

    /** The world inventory every item lying in the world belongs to, or nullptr. */
    SDK::wh::entitymodule::C_WorldInventory *WorldInventory();

    /** An item by WUID wherever it is held -- world, pile, inventory or a carry's own holder -- or nullptr. One lookup. */
    SDK::wh::entitymodule::C_Item *FindItem(std::uint64_t itemWuid);

    /**
     * Makes `amount` of `itemClass` in world inventory and answers the items
     * it became, without a pickable.
     *
     * `CreateItems` says nothing about which stacks it made, so they are
     * found by difference. Creation can split or partly succeed, so the
     * caller checks what it got and removes what it does not want.
     */
    std::vector<SDK::wh::entitymodule::C_Item *> CreateWorldItems(const Shared::RPC::ItemClassId &itemClass, std::uint32_t amount, std::uint32_t quality = 0, float health = -1.0F, float condition = -1.0F);

    /**
     * Everything about ground stacks that is not about one stack.
     *
     * The server holds the player's inventory, so a drop and a pickup are
     * asked of it rather than done natively: a drop names the server rows the
     * selection holds, and the server removes them and spawns the stacks; a
     * pickup claims one replica, and the server adds it to the inventory. The
     * inventory sync then moves the native items either way.
     */
    class GroundItemService final: public KCDC::Core::IFeatureService {
      public:
        /** Registers the replicated type and the pickup RPC. Once at startup. */
        static void Install();

        /** Game thread: settles pickup verdicts and drives the stacks. */
        void Update() override;

        /** Drops the pickup in flight, for a session that is going away. */
        void Reset() override;

        // --- Native item-transfer edges, raised from core/hooks ------------

        /** Asks the server to drop `amount` of the local stack `wuid` (0: all of it). True when the native drop must not run. */
        bool RequestDrop(std::uint64_t wuid, std::uint32_t amount, bool held);

        /** Brackets one native UI selection, whose stack callbacks share a request. */
        void BeginDropSelection();
        void FinishDropSelection();

        /** A native path other than the player's drop put a local item into the world. */
        void OnLocalDrop(SDK::wh::entitymodule::C_Item *droppedItem, bool retiresHeldItem);

        /** Claims the replica behind `descriptor` from the server, if it is one. */
        bool OnPickupStarting(SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor, bool animated);

        /**
         * The local player chose the game's pick-up over the item `descriptor`
         * names. For a carryable prop that is a carry, never a pickup: asks
         * the server for it and returns true, and the native pickup must not
         * run.
         */
        bool RequestCarryInstead(const SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor);

        /**
         * Asks the server for a carry of this prop: it claims it, asks
         * `playerCarryingItem`, and answers with the order. One ask per prop
         * while the answer is in flight.
         */
        void RequestCarry(GroundItem &prop);

        /** The prop whose world item `itemWuid` is, or nullptr. */
        static GroundItem *FindPropByNativeItem(std::uint64_t itemWuid);

        /** Whether the claimed pickup is the one `descriptor` names. */
        bool IsPickupTrackedFor(const SDK::wh::entitymodule::S_ItemTransferDescriptor *descriptor) const;

        /** The native pickup call returned; the server's verdict is what settles it. */
        void OnPickupSettled(bool succeeded);

        /** The native pickup was cancelled or refused. */
        void OnPickupCancelled();

        /**
         * Whether a matching drop still needs the given player's hand shadow.
         * Completed drops do not delay later draws or holsters.
         */
        static bool HasPendingHeldItemDrop(std::uint64_t sourcePlayerNetworkId, const Shared::RPC::ItemClassId &heldClass);

      private:
        /** A pickup claimed from the server, until its verdict. */
        struct PendingPickup {
            std::uint64_t networkId = 0;
            std::uint64_t token     = 0;
            std::uint64_t wuid      = 0;
            std::uint64_t expiresAt = 0;
        };

        void HandlePickupResult(const Shared::RPC::GroundItemPickupResult &result);

        /**
         * Offers "Pick up" over the nearest carryable prop within reach.
         *
         * The game draws no prompt over an NPC tool -- a basket, a bucket, the
         * firewood -- lying in the world: only the item classes its tables
         * script as `CarryableItem` get the carry prompt. So the prompt is
         * this one, on the game's own key and in the game's own words
         * (`@ui_pickup_item`), shown while the player's hands are free.
         */
        void UpdateCarryPrompt();
        void HideCarryPrompt();

        std::mutex _incomingMutex;
        std::vector<Shared::RPC::GroundItemPickupResult> _incoming;
        std::optional<PendingPickup> _pendingPickup;
        std::uint64_t _nextPickupToken = 1;
        // The prop last asked to carry, and until when another ask waits.
        std::uint64_t _carryAskedFor = 0;
        std::uint64_t _carryAskedUntil = 0;

        // The "Pick up" hint up over a prop, and which prop it is for.
        std::uint32_t _carryPrompt     = 0;
        std::uint64_t _carryPromptProp = 0;
        std::uint64_t _nextCarryPromptCheck = 0;
        KCDC::Shared::Inventory::DropSelection _dropSelection;
    };
} // namespace KCDC::Features::GroundItem
