#pragma once

#include "features/stash/stash_contents.h"
#include "shared/features/stash/stash_entity.h"
#include "shared/features/stash/stash_rpc.h"
#include "shared/game/stash_catalog.h"
#include "shared/item_stack.h"

#include <mafianet/types.h>
#include <mafianet/virtual_world.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KCDC::Shared::RPC {
    struct StashInventoryTransfer;
} // namespace KCDC::Shared::RPC

namespace KCDC::Features::Stash {
    /**
     * Server authority for replicated containers: which exist, what is in
     * each, who has one open, whether it is locked, and whether a player may
     * open, unlock or pick one.
     *
     * What a container holds is an in-memory inventory the server keeps for
     * it, empty until a player or a script puts something in. The player who
     * has it open moves units in and out, one request at a time; nothing
     * their game shows ever becomes stock.
     *
     * Every container has a catalog entry, keyed by the EntityGuid every
     * client finds it under. A level container's comes from the compiled
     * stash catalog and is built for every placement at boot, exactly as doors
     * are: a client never tells the server one exists. A spawned container's
     * is appended when a script spawns it and dropped when it goes, so the
     * two are found, measured and gated the same way.
     *
     * Every use of either is asked for before it happens. The client holds
     * the game's own prompt handler, the server checks the game's rules and
     * then `stashInteract`, and only an allowed request runs -- so a refused
     * open never shows the transfer screen. What the client reports after
     * that (the open, a key turned, a lock picked) is accepted only against
     * the grant the server gave it.
     */
    class StashService final {
      public:
        void Install(const std::string &level);
        void Update();
        void Reset();
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid);

        /**
         * Creates one empty container, built with `model` as its
         * `object_Model` or AnimStash.lua's own chest when it is empty;
         * `announce` false leaves raising `stashSpawn` to the caller.
         */
        Shared::Entities::StashEntity *Spawn(const glm::vec3 &position, const glm::quat &rotation, std::uint32_t virtualWorld, const std::string &model = {}, bool announce = true);

        /** Whether every client can build a container with this `object_Model`: a .cgf, .cga or .cdf path. */
        static bool IsSpawnableModel(const std::string &model);

        /** Creates empty stock without a world entity. Only server scripts may open it. */
        Shared::Entities::StashEntity *CreateVirtual(std::uint32_t virtualWorld);

        /** Opens a virtual container for one connected player, subject to stashInteract. */
        bool OpenVirtual(Shared::Entities::StashEntity &stash, std::uint64_t playerNetworkId);

        /** Removes one spawned container and forgets its contents. False for a level container or one this server does not own. */
        bool Despawn(Shared::Entities::StashEntity *stash);

        /** Removes scripted chests and virtual stock in one world or all worlds. Level containers stay. */
        std::size_t DespawnAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld = std::nullopt);

        /** Every live container, in one virtual world or in all of them. */
        std::vector<Shared::Entities::StashEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const;

        /** How many rows the server is holding for one container. */
        std::size_t ContentCount(std::uint64_t stashNetworkId) const;

        /** The body that has this container's contents open, or 0 when nobody has. */
        std::uint64_t HolderOf(std::uint64_t stashNetworkId) const;

        /** The stock a container opens: its master's for a linked child. Null for a container this server does not know. */
        KCDC::Features::Inventory::Inventory *ContentsOf(Shared::Entities::StashEntity &stash);

        /**
         * Spawns a container at a player's body and moves their items into it
         * (reason `drop`), keeping the units they wear when `keepEquipped`.
         * Null, with no container left behind, when nothing would move.
         */
        Shared::Entities::StashEntity *DropInventory(std::uint64_t playerId, bool keepEquipped);

        /** After a script changed a container's stock: tells the player who has it open, and raises `stashInventoryChanged`. */
        void ContentsChanged(Shared::Entities::StashEntity &stash);

        /** The container catalogued under one EntityGuid in one virtual world, level or spawned, or null. */
        Shared::Entities::StashEntity *Find(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld) const;

        /**
         * As Find, but a container the level places is built on first ask in a
         * virtual world other than the global one, where every one already
         * exists from boot. A spawned container lives in the world it was
         * spawned in only. Null for a GUID neither the level nor a script placed.
         */
        Shared::Entities::StashEntity *Resolve(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld);

        /** The container nearest a point in one virtual world, level or spawned, within `maxDistance`; null for none. */
        Shared::Entities::StashEntity *Nearest(const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld, float maxDistance);

        /** A container's catalog entry: the level's placement, or the one appended when it was spawned. */
        const Shared::Game::StashPlacement *PlacementOf(const Shared::Entities::StashEntity &stash) const;

        /**
         * Server control over one container's lock. Locking from here marks it
         * `lockedByServer`, which the generated home or shop key does not open;
         * unlocking by any route clears that.
         */
        void SetLock(Shared::Entities::StashEntity &stash, bool locked);

        void SetLockpickable(Shared::Entities::StashEntity &stash, bool lockpickable);

        /** The minigame's difficulty, 0 to 1, or `StashEntity::kNativeLockDifficulty` for the game's own; the caller validates it. */
        void SetLockDifficulty(Shared::Entities::StashEntity &stash, float difficulty);

        void SetLockFanciness(Shared::Entities::StashEntity &stash, Shared::Entities::StashEntity::LockFanciness fanciness);

        /** The class every client asks for instead of the level's own key; an invalid class restores the level's. */
        void SetKeyItem(Shared::Entities::StashEntity &stash, const Shared::RPC::ItemClassId &keyItemClass);

      private:
        friend class StashServiceTestAccess;

        enum class SnapshotAccess {
            HeldView,
            OpenGrant,
        };

        /** What one connection was allowed to do to one container, until when. */
        struct Grant {
            MafiaNet::PeerGuid peer         = MafiaNet::UNASSIGNED_PEER_GUID;
            std::uint64_t stashNetworkId    = 0;
            std::uint64_t contentsNetworkId = 0;
            Shared::RPC::StashAction action = Shared::RPC::StashAction::Open;
            std::uint64_t expiresAt         = 0;
        };

        struct CatalogKey {
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            std::uint64_t guid                    = 0;

            bool operator==(const CatalogKey &other) const {
                return virtualWorld == other.virtualWorld && guid == other.guid;
            }
        };

        struct CatalogKeyHash {
            std::size_t operator()(const CatalogKey &key) const;
        };

        struct CatalogRecord {
            Shared::Entities::StashEntity *entity         = nullptr;
            const Shared::Game::StashPlacement *placement = nullptr; // never null once indexed
        };

        /**
         * A catalog entry appended at runtime for a spawned container. Owns
         * the strings its placement points at; held by pointer, so the
         * placement's address outlives any rehash of the map it sits in.
         */
        struct RuntimePlacement {
            Shared::Game::StashPlacement placement {};
            std::string name;
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
        };

        /** Drops a spawned container's catalog entry and its index record. */
        void ForgetSpawned(Shared::Entities::StashEntity &stash);

        /** Creates one level container in one virtual world, in the state the level builds it in. */
        Shared::Entities::StashEntity *Materialize(const Shared::Game::StashPlacement &placement, MafiaNet::VirtualWorldId virtualWorld);

        /** The container whose contents and lease a container uses: its master's, or its own. */
        static std::uint64_t ContentsIdOf(Shared::Entities::StashEntity &stash);

        void OnTransfer(const Shared::RPC::StashInventoryTransfer &transfer, MafiaNet::Packet *packet);
        void OnOpened(const Shared::RPC::StashOpened &opened, MafiaNet::Packet *packet);
        void OnClosed(const Shared::RPC::StashClosed &closed, MafiaNet::Packet *packet);
        void OnInteractRequest(const Shared::RPC::StashInteractRequest &request, MafiaNet::Packet *packet);
        void OnLockReport(const Shared::RPC::StashLockReport &report, MafiaNet::Packet *packet);

        /** The live grant this connection holds on a container, or null. Matched on the container itself or on the contents it opens. */
        Grant *FindGrant(MafiaNet::PeerGuid peer, std::uint64_t stashNetworkId, bool byContents);

        void SendVerdict(MafiaNet::PeerGuid guid, const Shared::RPC::StashInteractRequest &request, bool allowed) const;
        bool CanAccessContainer(const Shared::Entities::HumanEntity &player, const Shared::Entities::StashEntity &stash) const;
        /** World, readiness, lock and exclusive access for a script opening virtual stock; never distance. */
        bool CanOpenVirtual(Shared::Entities::StashEntity &stash, std::uint64_t playerNetworkId) const;
        void SendSnapshot(std::uint64_t playerNetworkId, std::uint64_t stashNetworkId, const std::string &code, SnapshotAccess access = SnapshotAccess::HeldView);
        static StashIdentity IdentityOf(Shared::Entities::StashEntity &stash);
        /** Lowers the lid of a container its viewer let go of. */
        void Released(const StashIdentity &stash);
        /** Tells the holder of a container about to go that its screen closes. */
        void NotifyHolderClosed(const StashIdentity &stash);
        /** Tells one player the server closed their view of a container. */
        void SendClosed(std::uint64_t playerNetworkId, std::uint64_t stashNetworkId) const;
        /** Every container sharing this stock shows whether it holds anything. */
        void RefreshHasContents(std::uint64_t contentsId);
        /** Raises what happened to containers, after the inventory changes it caused. */
        void DispatchEvents();

        std::unique_ptr<StashContents> _contents;
        std::unordered_map<CatalogKey, CatalogRecord, CatalogKeyHash> _index;

        /** This level's slice of the compiled stash catalog, by GUID. */
        std::unordered_map<std::uint64_t, const Shared::Game::StashPlacement *> _catalog;

        /** The entries appended for spawned containers, by GUID. */
        std::unordered_map<std::uint64_t, std::unique_ptr<RuntimePlacement>> _runtimeCatalog;
        std::vector<Grant> _grants;

        /** A synchronous stashInteract handler cannot recursively open the same stock. */
        std::unordered_set<std::uint64_t> _virtualOpenRequests;

        /** When each connection last asked, so a flood of requests cannot run the script event for each. */
        std::unordered_map<MafiaNet::PeerGuid, MafiaNet::Time> _lastRequestAt;
        std::uint64_t _nextStashId = 1;
        std::string _level;
    };
} // namespace KCDC::Features::Stash
