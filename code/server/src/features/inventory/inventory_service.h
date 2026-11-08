#pragma once

#include "inventory.h"
#include "inventory_gameplay.h"
#include "shared/features/inventory/inventory_gameplay_message.h"
#include "shared/features/inventory/inventory_message.h"
#include "shared/features/inventory/inventory_quickslots.h"
#include "shared/features/inventory/item_stats_message.h"
#include "shared/game/clothing_presets.h"
#include "shared/game/item_classes.h"
#include "shared/rate_limit.h"

#include <mafianet/types.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace KCDC::Features::Inventory {
    /**
     * Every connected player's inventory, in memory for their session.
     *
     * An inventory is created empty when the player connects and dropped
     * after `playerDisconnect`, which can still read it. The server stores
     * nothing: scripts save from `playerInventoryChanged` and restore with
     * `Inventory.set`.
     *
     * The owner's native inventory is a projection of these rows. Changes are
     * published once per tick, as the rows that changed since what the client
     * last received; the client acknowledges each revision it projected, and
     * the first acknowledgement raises `playerInventoryReady`.
     *
     * The owner also reports what its game did with those items: a use or a
     * shot spends units, the worn set is recorded per session, and a round
     * that came to rest is laid down again as a ground item.
     */
    class InventoryService final {
      public:
        void Install();
        void PublishDefinitions();
        void Receive(const KCDC::Shared::RPC::CustomItemUse &message, MafiaNet::PeerGuid peer);
        nlohmann::json ScriptUpdateCustom(std::uint64_t playerId, const nlohmann::json &request);
        void Update();
        void Reset();
        void OnPlayerConnect(MafiaNet::PeerGuid peer, std::uint64_t playerId);
        void OnPlayerReady(MafiaNet::PeerGuid peer);
        void OnPlayerDisconnect(MafiaNet::PeerGuid peer);
        /** Raises every pending change now, before a script event that must see them in order. */
        void DispatchChanges();

        void Receive(const KCDC::Shared::RPC::InventoryApplied &message, MafiaNet::PeerGuid peer);
        void Receive(const KCDC::Shared::RPC::InventoryWear &message, MafiaNet::PeerGuid peer);
        void Receive(const KCDC::Shared::RPC::InventoryConsume &message, MafiaNet::PeerGuid peer);
        void Receive(const KCDC::Shared::RPC::InventoryEquipment &message, MafiaNet::PeerGuid peer);
        void Receive(const KCDC::Shared::RPC::InventoryRecover &message, MafiaNet::PeerGuid peer);

        Inventory *Find(std::uint64_t playerId);
        /** The row identities every inventory draws from, a player's or a stash's. */
        InventoryIds &Ids() {
            return _ids;
        }
        /** Worn units per row, as the owner last reported them or a script set them; null for a player who is not connected. */
        const std::map<std::string, std::uint32_t> *Equipped(std::uint64_t playerId) const;
        /** True once the owner acknowledged the current revision. */
        bool IsReady(std::uint64_t playerId) const;
        /**
         * Advances on every inventory restore, including restores with
         * unchanged rows; null for an absent session.
         */
        std::optional<std::uint64_t> RestoreGeneration(std::uint64_t playerId) const;
        /**
         * True after the owner's first successful projection, including
         * while later revisions are in flight.
         */
        bool IsInitialized(std::uint64_t playerId) const;
        /** Spends one of the player's request tokens: false when they are sending too fast; a flood disconnects them on the next tick. */
        bool Admit(std::uint64_t playerId);

        /** `player.giveItem` and `player.takeItem`; both are announced in the player's game. `Inventory.add` and `remove` can stay quiet. */
        InventoryResult Grant(std::uint64_t playerId, const std::string &itemClass, std::uint32_t amount);
        InventoryResult Take(std::uint64_t playerId, const std::string &itemClass, std::uint32_t amount);
        /**
         * A round of `fired` that `shooterId` shot and that lodged in a body:
         * taken from the shooter's spent rounds and put into `target`, the
         * struck body's inventory, as the game does. False when no such round
         * is spent or the target refuses it.
         */
        bool Lodge(std::uint64_t shooterId, const std::string &fired, Inventory *target);

        nlohmann::json ScriptGet(std::uint64_t playerId) const;
        nlohmann::json ScriptAdd(std::uint64_t playerId, const nlohmann::json &request);
        nlohmann::json ScriptRemove(std::uint64_t playerId, const nlohmann::json &request);
        nlohmann::json ScriptSetProperties(std::uint64_t playerId, const nlohmann::json &request);
        nlohmann::json ScriptSet(std::uint64_t playerId, const nlohmann::json &request);
        nlohmann::json ScriptTransfer(std::uint64_t sourceId, std::uint64_t targetId, const nlohmann::json &request);
        /**
         * `player.setOutfit`: takes off every piece of clothing the body
         * wears and dresses it in a fresh set of the preset's, all through one
         * `set`. Clothing taken off stays in the inventory; weapons and
         * everything else keep what they were.
         */
        nlohmann::json ScriptWearOutfit(std::uint64_t playerId, const KCDC::Shared::Game::ClothingPresetEntry &preset);

        /** The same requests on any inventory: a stash's, for one. Only a player's game announces a change, so `notify` is the caller's to give. */
        static nlohmann::json ScriptAdd(Inventory *inventory, const nlohmann::json &request, bool notify = false);
        static nlohmann::json ScriptRemove(Inventory *inventory, const nlohmann::json &request, bool notify = false);
        static nlohmann::json ScriptSet(Inventory *inventory, const nlohmann::json &request);
        static nlohmann::json ScriptSetProperties(Inventory *inventory, const nlohmann::json &request);
        static nlohmann::json ScriptTransfer(Inventory *source, Inventory *target, const nlohmann::json &request);

        static nlohmann::json EncodeInventory(const Inventory &inventory);

        /**
         * Names an item class on every client, now and for whoever joins
         * later; an empty name gives it back to the game. The game names
         * classes through shared keys, so every class sharing this one's
         * `nameKey` changes with it. False when the class has no key or the
         * name is not one a client would take.
         */
        bool SetItemName(const KCDC::Shared::Game::ItemClassEntry &item, const std::string &name);

        /**
         * Rewrites the combat numbers of an item class on every client, now
         * and for whoever joins later: a melee weapon's attack and its
         * stab/slash/smash modifiers, an armor piece's three defenses and
         * noise, a bow's power, an arrow's attack, the requirements. The
         * numbers live on the class row the game reads on every hit, so the
         * change is live for the next swing on every client. An empty set
         * gives the class its authored numbers back. False for a class whose
         * table element has none of these, or a stat that element lacks.
         */
        bool SetItemStats(const KCDC::Shared::Game::ItemClassEntry &item, const std::vector<KCDC::Shared::RPC::ItemStats::Value> &values);

        /** The server's override of one class, or null for an untouched class. */
        const std::vector<KCDC::Shared::RPC::ItemStats::Value> *ItemStatsOf(const KCDC::Shared::Game::ItemClassEntry &item) const;

      private:
        struct Session {
            explicit Session(InventoryIds &ids, std::uint64_t id): inventory(ids), playerId(id) {}

            std::optional<std::uint64_t> registryRevision;
            Inventory inventory;
            std::uint64_t playerId = 0;
            bool ready             = false; // the client can receive syncs
            bool announced         = false; // playerInventoryReady was raised
            bool resync            = true;  // the next publish is a full one
            std::optional<std::uint64_t> publishedRevision;
            std::optional<std::uint32_t> publishedWorld;
            std::uint64_t acknowledgedRevision = 0;
            bool projectionAccepted            = false;
            std::map<std::string, nlohmann::json> published;
            // Published rows the owner's game last said it could not show.
            std::set<std::string> unshown;
            KCDC::Shared::RateLimit soft {20.0, 40.0};
            KCDC::Shared::RateLimit flood {60.0, 120.0};
            bool flooding = false;
            std::map<std::string, std::uint32_t> equipped;
            std::optional<Shared::Inventory::Quickslots> quickslots;
            bool equipmentChanged = false;
            // A report the owner sent before a script's restore reached it
            // describes the body before the restore, and is ignored.
            std::uint64_t equipmentRevision = 0;
            // Advances on every script restore, even when its item rows did not change.
            std::uint64_t equipmentRestore = 0;
            // Ammunition rows that have been in the quiver: the only rows a shot spends.
            std::set<std::string> loaded;
            std::optional<std::uint64_t> clampedRevision;
            SpentRounds spent;
            struct FoodUse {
                std::uint64_t sequence    = 0;
                std::uint64_t expires     = 0;
                std::uint32_t incarnation = 0;
                std::uint32_t world       = 0;
                std::uint64_t restore     = 0;
                bool pending              = false;
                bool refundPending        = false;
                InventoryRow row;
                nlohmann::json description;
            } foodUse;
        };
        struct ItemUsed {
            std::uint64_t playerId = 0;
            std::string itemClass;
            std::uint32_t amount = 0;
        };
        enum class MessageKind : std::uint8_t {
            CustomUse,
            Applied,
            Wear,
            Consume,
            Equipment,
            Recover,
        };
        struct Incoming {
            MafiaNet::PeerGuid peer;
            MessageKind kind = MessageKind::Applied;
            std::string body;
        };

        Session *SessionOf(std::uint64_t playerId);
        const Session *SessionOf(std::uint64_t playerId) const;
        void Queue(MafiaNet::PeerGuid peer, MessageKind kind, const KCDC::Shared::RPC::InventoryMessage &message);
        void CustomUse(Session &session, const nlohmann::json &message);
        void Applied(Session &session, const nlohmann::json &message);
        void Wear(Session &session, const nlohmann::json &message);
        void Consume(Session &session, const nlohmann::json &message);
        void UseFood(Session &session, const nlohmann::json &message);
        void Prepare(Session &session, const nlohmann::json &message);
        void RefundFood(Session &session);
        void AnswerConsume(const Session &session, const std::string &request, bool ok, const std::string &code) const;
        static void Wears(Session &session, std::map<std::string, std::uint32_t> equipped);
        void Equipment(Session &session, const nlohmann::json &message);
        void Recover(Session &session, const nlohmann::json &message);
        void Publish(Session &session);

        InventoryIds _ids;
        std::map<MafiaNet::PeerGuid, Session> _sessions;
        std::map<std::uint64_t, MafiaNet::PeerGuid> _peers;
        std::mutex _incomingMutex;
        std::vector<Incoming> _incoming;
        // Raised after the tick's changes, so a handler reads the rows they left.
        std::vector<ItemUsed> _itemUsed;
        struct CustomUseEvent {
            std::uint64_t playerId;
            nlohmann::json row;
            std::uint64_t revision;
        };
        std::vector<CustomUseEvent> _customUses;
        // Lowercase name key, as the engine compares them, to the server's name.
        std::map<std::string, std::string> _itemNames;
        // Canonical class GUID to the stats the server rewrote on it.
        std::map<std::string, std::vector<KCDC::Shared::RPC::ItemStats::Value>, std::less<>> _itemStats;
    };

    void RegisterInventoryHandlers(InventoryService &service);
} // namespace KCDC::Features::Inventory
