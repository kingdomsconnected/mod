#pragma once

#include "features/inventory/inventory.h"
#include "shared/features/stash/stash_rpc.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::Stash {
    /** One physical container, and the one whose stock it opens: its master's for a linked child, else its own. */
    struct StashIdentity {
        std::uint64_t networkId    = 0;
        std::uint64_t contentsId   = 0;
        std::uint64_t guid         = 0;
        std::uint32_t virtualWorld = 0;
    };

    /** `{ "stash", "deposit", "units", "playerRevision", "stashRevision" }` from the holder. */
    struct StashTransfer {
        std::uint64_t stash = 0;
        bool deposit        = false;
        std::vector<KCDC::Features::Inventory::InventoryUnit> units;
        std::uint64_t playerRevision = 0;
        std::uint64_t stashRevision  = 0;
    };

    /** stashOpen, stashClose, stashInventoryChanged or stashDestroy, in the order they happened. */
    struct StashEvent {
        std::string name;
        StashIdentity stash;
        std::uint64_t playerId = 0;
        std::string reason;
        std::optional<KCDC::Features::Inventory::InventoryChange> change;
    };

    /**
     * What every container holds, and who has each one open.
     *
     * Stock is an in-memory inventory per container that owns it, created
     * empty and gone with the container: nothing is saved, and a script
     * restores what it wants from the events. One player views a container's
     * stock at a time, and only that player moves units in or out of it.
     */
    class StashContents final {
      public:
        /** Covers the client that never reports its close; the holder's game renews the view while its screen is up. */
        static constexpr std::uint64_t kViewTimeoutMs = 120000;

        /** Only an open shows what is inside: a key or a pick has not beaten the lock yet. */
        static bool RevealsContents(KCDC::Shared::RPC::StashAction action) {
            return action == KCDC::Shared::RPC::StashAction::Open;
        }

        explicit StashContents(KCDC::Features::Inventory::InventoryIds &ids): _ids(ids) {}

        static std::uint64_t ContentsIdOf(std::uint64_t networkId, std::uint64_t contentsNetworkId) {
            return contentsNetworkId != 0 ? contentsNetworkId : networkId;
        }
        static bool ReadTransfer(const std::string &body, StashTransfer &transfer);
        static bool CanAccess(std::uint32_t stashWorld, std::uint32_t playerWorld, bool canAct, float distance, float maxDistance, bool isVirtual = false);
        /** `{ "stash", "revision", "code", "rows" }` for the holder. */
        static nlohmann::json Snapshot(std::uint64_t stashNetworkId, const KCDC::Features::Inventory::Inventory &inventory, const std::string &code);

        KCDC::Features::Inventory::Inventory &Create(std::uint64_t contentsId);
        KCDC::Features::Inventory::Inventory *Find(std::uint64_t contentsId);
        /** Whether a container holds anything, for the prompt every client shows. */
        bool HasContents(std::uint64_t contentsId) const;

        bool Open(const StashIdentity &stash, std::uint64_t playerId, std::uint64_t nowMs);
        bool Close(const StashIdentity &stash, std::uint64_t playerId, const std::string &reason);
        /** The holder is still looking: the view's deadline moves on. False for anyone else. */
        bool Renew(std::uint64_t contentsId, std::uint64_t playerId, std::uint64_t nowMs);
        /** The player viewing this stock, or 0. */
        std::uint64_t HolderOf(std::uint64_t contentsId) const;
        /** The physical container this stock is open through, or null. */
        const StashIdentity *ViewOf(std::uint64_t contentsId) const;

        KCDC::Features::Inventory::InventoryResult Transfer(const StashIdentity &stash, std::uint64_t playerId, KCDC::Features::Inventory::Inventory &player, const StashTransfer &transfer);
        /** Puts a player's units in with nobody holding it open: a death drop. */
        KCDC::Features::Inventory::InventoryResult Deposit(const StashIdentity &stash, std::uint64_t playerId, KCDC::Features::Inventory::Inventory &player, const std::vector<KCDC::Features::Inventory::InventoryUnit> &units);
        /** Reports what changed in a container's stock since the last report. */
        void Changed(const StashIdentity &stash, std::uint64_t playerId);

        /** Every open view: the container it is open through and its holder. */
        std::vector<std::pair<StashIdentity, std::uint64_t>> Views() const;
        /** Closes views past their deadline; returns the containers that closed. */
        std::vector<StashIdentity> Expire(std::uint64_t nowMs);
        /** Closes whatever one player had open. */
        std::vector<StashIdentity> Release(std::uint64_t playerId);
        /** Closes it, empties its stock if it owns any, and forgets it. */
        void Destroy(const StashIdentity &stash);

        std::vector<StashEvent> TakeEvents();

      private:
        struct View {
            StashIdentity stash;
            std::uint64_t playerId  = 0;
            std::uint64_t expiresAt = 0;
        };

        void End(std::uint64_t contentsId, const std::string &reason);

        KCDC::Features::Inventory::InventoryIds &_ids;
        std::map<std::uint64_t, std::unique_ptr<KCDC::Features::Inventory::Inventory>> _stock;
        std::map<std::uint64_t, View> _views;
        std::vector<StashEvent> _events;
    };
} // namespace KCDC::Features::Stash
