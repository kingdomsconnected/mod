#pragma once

#include "shared/features/door/door_definition.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/door/door_policy.h"
#include "shared/features/door/door_rpc.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/door_catalog.h"

#include <mafianet/virtual_world.h>
#include <mafianet/types.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace KCDC::Features::Door {
    /**
     * Server authority over doors.
     *
     * Doors are replicated entities, so this owns none of their state. It is
     * the GUID index, built from the level's door catalog and from the doors
     * scripts and world resources hang; the gate a client's report of working
     * a door has to pass, under the game's own lock rules and then a
     * script's; and the relay that carries an accepted interaction to the
     * players who can watch it.
     *
     * A level door a level edit takes out of a world is gone from that world
     * here too: listed nowhere, never built, and refused when reported. The
     * edit is the record, so a world resource that deletes a door in the
     * World Builder removes it the same way a script does.
     */
    class DoorService final {
      public:
        /**
         * What a client answered when asked which door its player is at.
         *
         * `found` is false for nothing in sight, another level's door, or no
         * answer at all; `reason` says which.
         */
        struct NearestDoor {
            bool found = false;
            std::string reason;
            std::uint64_t doorGuid = 0;
            std::string name;
            float distance = 0.0F;
            bool open      = false;
            bool locked    = false;
        };

        /** Called once per query, on the server tick, with whatever came back. */
        using NearestDoorHandler = std::function<void(const NearestDoor &)>;

        /** A level door a level edit has taken out of a world. */
        struct RemovedDoor {
            std::uint64_t doorGuid                             = 0;
            MafiaNet::VirtualWorldId virtualWorld              = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            const KCDC::Shared::Game::DoorPlacement *placement = nullptr;
        };

        void Install(const std::string &level);
        void Update();

        /**
         * Folds one client's report of working a door into the door.
         *
         * Refused when the body is out of reach, when the game's own lock
         * rules forbid it, or when a script's `doorInteract` handler returns
         * false. A refused report is not relayed, and the door's state is
         * re-sent so the reporter's client puts its own door back the way the
         * server has it. One that changes nothing is re-sent the same way but
         * still relayed, so viewers watch the body that worked the door.
         */
        void ProcessInteraction(const KCDC::Shared::RPC::DoorInteraction &interaction, KCDC::Shared::Entities::HumanEntity &actor);
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid);

        /**
         * Server control over one door's lock. Does nothing to a door already
         * in that state. A door locked from here can only be opened again
         * from here or with a lockpick: a client's report of unlocking it by
         * hand or with a key is refused.
         */
        void SetLock(KCDC::Shared::Entities::DoorEntity &door, bool locked);

        /**
         * Server control over whether one door stands open. Clients that
         * already have the door built play the swing; the rest find it posed.
         * Opening a locked door unlocks it, as AnimDoor:Open does. False when
         * the door is already that way.
         */
        bool SetOpen(KCDC::Shared::Entities::DoorEntity &door, bool open, bool fromFront);

        /**
         * Whether players may work one door. Every client that has it
         * streamed in hides its prompt and refuses a use before anything
         * moves; a report against it is refused here too. The door's state
         * is left alone, and NPCs still pass it.
         */
        void SetUsable(KCDC::Shared::Entities::DoorEntity &door, bool usable);

        /** Navigation reads this world without creating replicas during path searches. */
        bool CanNavigate(std::uint64_t guid, MafiaNet::VirtualWorldId world) const;
        Shared::Game::DoorPassage PreparePassage(std::uint64_t guid, MafiaNet::VirtualWorldId world, const glm::vec3 &position, MafiaNet::Time now);

        /** What the level says about a door; null only for a handle that has gone. */
        const KCDC::Shared::Game::DoorPlacement *PlacementOf(const KCDC::Shared::Entities::DoorEntity &door) const;

        /** Whether the server, rather than the level, locked this door. */
        bool IsLockedByServer(const KCDC::Shared::Entities::DoorEntity &door) const;

        /**
         * Asks the caller's client which door its player is standing at.
         *
         * `handler` runs exactly once -- on the answer, on the timeout, or on
         * a disconnect -- so a promise built on it always settles. False when
         * there is no peer to ask, and then it does not run at all.
         */
        bool QueryNearest(KCDC::Shared::Entities::HumanEntity &caller, NearestDoorHandler handler);

        /** The replicated door for one level GUID in one virtual world, or null. */
        KCDC::Shared::Entities::DoorEntity *Find(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld) const;

        /**
         * As Find, but a door the level places is built on first ask in a
         * virtual world other than the global one, where every door already
         * exists from boot. Null only for a GUID the level does not place.
         */
        KCDC::Shared::Entities::DoorEntity *Resolve(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld);

        /** Every door this server knows, in one virtual world or in all of them. */
        std::vector<KCDC::Shared::Entities::DoorEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const;

        /**
         * Hangs a door in one virtual world, closed, under a GUID every client
         * builds it by. The caller (a script, a world resource) owns it: it
         * goes with `Despawn`, or with the resource that made it. Null, with
         * `error` saying why, for an unknown model, a transform that is not
         * finite, or a key that is not an item class.
         */
        KCDC::Shared::Entities::DoorEntity *Spawn(const KCDC::Shared::Game::DoorDefinition &definition, MafiaNet::VirtualWorldId virtualWorld, std::string &error);

        /** Takes a hung door down everywhere. Does nothing to one the level places; `Remove` takes those out. */
        void Despawn(KCDC::Shared::Entities::DoorEntity &door);

        /**
         * Takes a door the level places out of one virtual world, collision
         * and all, through a level edit every player there applies; out of
         * the global world, it is out of every world. Already out, it is a
         * success. False, with `error` saying why, for a door the level does
         * not place.
         */
        bool Remove(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld, std::string &error);

        /**
         * Puts back a level door `Remove` or any other level edit took out of
         * that world, closed and locked as the level builds it. False when
         * that world has no such removal; one made in the global world is
         * only put back from there.
         */
        bool Unremove(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld);

        /** Whether a level edit has the level door out of that world, or out of the global one. */
        bool IsRemoved(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld) const;

        /** Every level door a level edit has out of one virtual world, or out of any. */
        std::vector<RemovedDoor> CollectRemoved(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const;

        /** What a hung door was hung as, in the form `Spawn` takes it; null for a door the level places. */
        const KCDC::Shared::Game::DoorDefinition *DefinitionOf(const KCDC::Shared::Entities::DoorEntity &door) const;

        std::size_t Count() const {
            return _index.size();
        }

      private:
        struct DoorKey {
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            std::uint64_t doorGuid                = 0;

            bool operator==(const DoorKey &other) const {
                return virtualWorld == other.virtualWorld && doorGuid == other.doorGuid;
            }
        };

        struct DoorKeyHash {
            std::size_t operator()(const DoorKey &key) const;
        };

        struct DoorRecord {
            KCDC::Shared::Entities::DoorEntity *entity         = nullptr;
            std::uint64_t networkId                            = 0;
            const KCDC::Shared::Game::DoorPlacement *placement = nullptr; // never null once indexed
            bool lockedByServer                                = false;
            MafiaNet::Time passageReadyAt                      = 0;
        };

        /**
         * Whether a record's copy still exists. A copy a script asks for in a
         * world is built while that script runs, so its resource owns it and
         * destroys it when it stops (Resource::DestroyOwnedEntities) while the
         * record stays. NetworkIDs are never reused: an id that no longer
         * resolves is that copy gone, and its record is read as absent until
         * the door is asked for again and built anew.
         */
        static bool IsLive(const DoorRecord &record);

        struct PendingNearest {
            MafiaNet::PeerGuid sender = MafiaNet::UNASSIGNED_PEER_GUID;
            std::uint64_t expiresAt   = 0;
            NearestDoorHandler handler;
        };

        /** A door a script or a world resource hung: the placement every rule reads, and the strings it points into. */
        struct HungDoor {
            KCDC::Shared::Game::DoorPlacement placement {};
            KCDC::Shared::Entities::SpawnedDoor spawned;
            KCDC::Shared::Game::DoorDefinition definition;
            std::string name;
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
        };

        /** Creates one door in one virtual world, in the state the level builds it in, or as `spawned` hangs it. */
        KCDC::Shared::Entities::DoorEntity *Materialize(const KCDC::Shared::Game::DoorPlacement &placement, MafiaNet::VirtualWorldId virtualWorld, const KCDC::Shared::Entities::SpawnedDoor *spawned = nullptr);

        /**
         * Takes down every copy a level edit has taken out, and builds again
         * the global copy of every door whose edit has since gone -- however
         * it went: restored, or destroyed with the resource that made it.
         */
        void ReconcileRemovals();

        /** Drops a hung door whose copy is gone, destroyed with the resource that hung it. */
        void ForgetGoneHungDoors();

        DoorRecord *FindRecord(std::uint64_t doorGuid, MafiaNet::VirtualWorldId virtualWorld);
        const DoorRecord *FindRecord(const KCDC::Shared::Entities::DoorEntity &door) const;

        /** Re-sends a door's state unchanged, so every client puts it back the way the server has it. */
        static void Correct(KCDC::Shared::Entities::DoorEntity &door);
        void QueueNearestAnswer(const KCDC::Shared::RPC::DoorNearestAnswer &answer, MafiaNet::Packet *packet);
        void ResolveNearestAnswer(const KCDC::Shared::RPC::DoorNearestAnswer &answer, MafiaNet::PeerGuid sender);

        /** Settles one outstanding query and drops it, so its handler cannot run twice. */
        void SettleNearest(std::uint64_t requestId, const NearestDoor &answer);

        std::size_t RelayInteraction(KCDC::Shared::RPC::DoorInteraction &interaction, KCDC::Shared::Entities::HumanEntity &actor) const;

        std::unordered_map<DoorKey, DoorRecord, DoorKeyHash> _index;

        /** This level's slice of the door catalog, by GUID. */
        std::unordered_map<std::uint64_t, const KCDC::Shared::Game::DoorPlacement *> _catalog;

        /** Doors scripts and world resources hung, by GUID. Heap-held: `_index` points at their placements. */
        std::unordered_map<std::uint64_t, std::unique_ptr<HungDoor>> _hung;
        std::uint64_t _nextHungSerial = 1;

        /** Level doors whose copies this took down for a level edit, so it knows to build them again once the edit goes. */
        std::vector<DoorKey> _removed;
        std::uint64_t _seenEditRevision = 0;
        std::mutex _incomingMutex;
        std::vector<std::pair<KCDC::Shared::RPC::DoorNearestAnswer, MafiaNet::PeerGuid>> _incomingNearestAnswers;
        std::unordered_map<std::uint64_t, PendingNearest> _pendingNearest;
        std::uint64_t _nextNearestRequestId = 1;
        std::string _level;
        std::uint32_t _levelHash = 0;
    };
} // namespace KCDC::Features::Door
