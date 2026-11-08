#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/game/door_catalog.h"

#include <mafianet/string.h>
#include <mafianet/virtual_world.h>
#include <networking/replication/network_entity.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Entities {
    /**
     * How a door a script or a world resource hangs is built: the leaf, how
     * it turns, and the lock AnimDoor.lua is given. A door the level places
     * has none of this on the wire -- every client already has it.
     */
    struct SpawnedDoor {
        /** A row of `g_DoorModels`. */
        std::uint16_t model = Shared::Game::kNoDoorModel;

        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};

        Shared::Game::DoorLockType lockType = Shared::Game::DoorLockType::ManualKey;

        /** `bCanLockPick`. */
        bool lockpickable = true;

        /** `fLockDifficulty`, from 0 to 1; negative leaves AnimDoor.lua's own. */
        float lockDifficulty = -1.0F;

        /** `guidItemClassId`: the item class that works the keyhole, as GUID text; empty for none. */
        std::string keyItem;

        /** A GUID's text is 36 characters; nothing longer is a key. */
        static constexpr std::size_t kMaxKeyItemLength = 36;
    };

    /**
     * One door, as the server knows it.
     *
     * The server builds one for every door the level places, from the
     * compiled door catalog and nothing else: a client never tells it that a
     * door exists. It also builds one for every door a script or a world
     * resource hangs, which carries what it is built from (`spawned`). Only
     * what survives a transition lives here -- the clip belongs to the
     * interaction, but the side it swung to is what a late joiner needs to
     * pose the leaf the same way.
     *
     * `doorGuid` is the level's own EntityGuid, or for a hung door the one
     * every client builds it under (`SpawnedGuid`).
     */
    class DoorEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Door";

        static constexpr float kStreamRange = 150.0F;

        /**
         * The high bits of the EntityGuid of a door the server hangs, above
         * the serial that makes it unique. A runtime spawn has to pick a GUID
         * no level entity has, and every client has to pick the same one.
         */
        static constexpr std::uint64_t kSpawnedGuidTag  = 0xF5A9000000000000ULL;
        static constexpr std::uint64_t kSpawnedGuidMask = 0x0000FFFFFFFFFFFFULL;

        static constexpr std::uint64_t SpawnedGuid(std::uint64_t serial) {
            return kSpawnedGuidTag | (serial & kSpawnedGuidMask);
        }

        static constexpr bool IsSpawnedGuid(std::uint64_t guid) {
            return (guid & ~kSpawnedGuidMask) == kSpawnedGuidTag;
        }

        std::uint64_t doorGuid  = 0;
        std::uint32_t levelHash = 0;

        /** What a hung door is built from; `spawned.model` is `kNoDoorModel` for one the level places. */
        SpawnedDoor spawned;

        bool IsSpawned() const {
            return spawned.model != Shared::Game::kNoDoorModel;
        }

        /**
         * The virtual world this copy is, as the server set it: the global
         * one for the copy built at boot, else the one world a script or a
         * player there asked for. The replica base keeps its world on the
         * server only, and a player can hold several copies of one door: the
         * global copy everyone sees, and every world's own copy when the
         * player is in the global world itself (Shared::Game::WorldCopyRank).
         */
        std::uint32_t copyWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;

        /** Bumped on every durable change; clients act on edges. */
        std::uint32_t stateToken = 0;

        bool open   = false;
        bool locked = false;

        /**
         * The side the leaf was last worked from, as AnimDoor.lua's
         * IsAccessedFromFront reads it. It picks which way the leaf swings,
         * so a client restoring an open door poses it from this rather than
         * from where its own player happens to stand.
         */
        bool openedFromFront = false;

        /**
         * The last change was made by the server rather than by a body. A
         * client that already has the door built plays it as a transition;
         * a body's change arrives separately as an interaction replay, so
         * that one is only ever restored as a pose.
         */
        bool changedByServer = false;

        /**
         * Whether a player may work the door at all, as a script set it. An
         * unusable door offers no prompt to open, close, unlock or pick it,
         * and its client refuses the use before anything moves; the server
         * refuses a report against it as well, for a client that did not.
         * It says nothing about NPCs, which open doors by the server's
         * navigation, nor about `Door.setOpen` and `locked`.
         */
        bool usable = true;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(doorGuid);
            fields.Field(levelHash);
            fields.Field(copyWorld);
            SerializeSpawned(fields);
            SerializeState(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeState(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
        }

      private:
        /** Construction-only: a door that changes its leaf is a new door. Nothing past the model travels for a level door. */
        void SerializeSpawned(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(spawned.model);
            if (spawned.model == Shared::Game::kNoDoorModel) {
                return;
            }
            fields.Field(spawned.rotation.x);
            fields.Field(spawned.rotation.y);
            fields.Field(spawned.rotation.z);
            fields.Field(spawned.rotation.w);

            std::uint8_t lockType = static_cast<std::uint8_t>(spawned.lockType);
            fields.Field(lockType);
            fields.Field(spawned.lockpickable);
            fields.Field(spawned.lockDifficulty);

            MafiaNet::RakString keyItem(spawned.keyItem.c_str());
            fields.Field(keyItem);
            if (!fields.Writing()) {
                spawned.lockType = static_cast<Shared::Game::DoorLockType>(std::min<std::uint8_t>(lockType, static_cast<std::uint8_t>(Shared::Game::DoorLockType::Key)));
                spawned.keyItem  = std::string(keyItem.C_String()).substr(0, SpawnedDoor::kMaxKeyItemLength);
            }
        }

        void SerializeState(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(stateToken);
            fields.Field(open);
            fields.Field(locked);
            fields.Field(openedFromFront);
            fields.Field(changedByServer);
            fields.Field(usable);
        }
    };
} // namespace KCDC::Shared::Entities
