#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/item_stack.h"

#include <networking/replication/network_entity.h>

#include <cstdint>
#include <string>

namespace KCDC::Shared::Entities {
    /**
     * A replicated container: one a server script spawned, one the level
     * places, or virtual stock with no world entity.
     *
     * Contents are not replicated: they are the server's in-memory rows,
     * sent to the one player who has the container open, and they change
     * only through the server. What that player's game shows never becomes
     * stock.
     *
     * A spawned container carries a `stashId` every client turns into the same
     * native EntityGuid. A level container carries `levelGuid` instead, the
     * EntityGuid the level already gave it, and is bound rather than built.
     * Exactly one of the two is non-zero. Virtual stock also has a stashId
     * for its native inventory GUID, but never creates a chest entity.
     */
    class StashEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Stash";

        /**
         * The high bits every container a script spawns carries in its
         * EntityGuid, above the `stashId` that makes it unique. A runtime spawn
         * has to pick a GUID the level did not: `C_Stash::Init` refuses one the
         * stash manager already holds.
         */
        static constexpr std::uint64_t kSpawnedGuidTag  = 0xF5A5000000000000ULL;
        static constexpr std::uint64_t kSpawnedGuidMask = 0x0000FFFFFFFFFFFFULL;

        /** The EntityGuid every client builds a spawned container under. */
        static constexpr std::uint64_t SpawnedGuid(std::uint64_t spawnedStashId) {
            return kSpawnedGuidTag | (spawnedStashId & kSpawnedGuidMask);
        }

        std::uint64_t stashId = 0;

        /** Server-created stock with no native entity or spatial interaction. */
        bool isVirtual = false;

        /** The level's own EntityGuid for a level container, else 0. */
        std::uint64_t levelGuid = 0;

        /**
         * The container whose contents this one opens, or 0 for its own. A
         * level stash with a `masterStash` link opens its master's inventory,
         * so the server keeps one set of contents and one lease for both.
         */
        std::uint64_t contentsNetworkId = 0;

        /** Someone has it open: the lid every observer animates, and no right to its contents. */
        bool isOpen = false;

        /** Its stock holds something: the prompt every client shows, without the rows. */
        bool hasContents = false;

        bool locked = false;

        /**
         * The lock was set by a script or an operator rather than by the level.
         * The generated home or shop key does not open one.
         */
        bool lockedByServer = false;

        bool lockpickable = true;

        /**
         * The item class that unlocks it, or zero for the level's own key.
         * Set by a script; every client asks for this class instead of the one
         * the level wrote.
         */
        Shared::RPC::ItemClassId keyItemClass;

        /** `lockDifficulty` when the game's own applies. */
        static constexpr float kNativeLockDifficulty = -1.0F;

        /**
         * The lockpicking minigame's `Properties.Lock.fLockDifficulty`, 0 to
         * 1, or `kNativeLockDifficulty` for what the level or AnimStash.lua
         * gave it. The game's prompt labels it in fifths, very easy to very
         * hard.
         */
        float lockDifficulty = kNativeLockDifficulty;

        /** `Properties.Lock.esLockFanciness`, which lock the minigame shows. */
        enum class LockFanciness : std::uint8_t {
            Native,
            Common,
            Fancy
        };
        LockFanciness lockFanciness = LockFanciness::Native;

        /**
         * The `object_Model` a spawned container is built with, or empty for
         * AnimStash.lua's own chest. A level container keeps the level's.
         */
        std::string model;

        Framework::Networking::Replication::NetworkEntity *GetInterestDependency() override {
            return contentsNetworkId != 0 ? ResolveSibling(contentsNetworkId) : nullptr;
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(stashId);
            fields.Field(isVirtual);
            fields.Field(levelGuid);
            fields.Field(contentsNetworkId);
            fields.Field(model);
            SerializeState(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeState(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(rotation.x, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.y, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.z, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.w, -1.0F, 1.0F, HumanEntity::kRotationBits);
        }

        /**
         * The EntityGuid every client finds this container under: the level's
         * own, or the one a spawned container is built with. What the server's
         * catalog is keyed by, for both.
         */
        std::uint64_t GetEntityGuid() const {
            return levelGuid != 0 ? levelGuid : SpawnedGuid(stashId);
        }

      private:
        void SerializeState(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.ServerField(isOpen);
            fields.ServerField(hasContents);
            fields.ServerField(locked);
            fields.ServerField(lockedByServer);
            fields.ServerField(lockpickable);
            fields.ServerField(keyItemClass.low);
            fields.ServerField(keyItemClass.high);
            fields.ServerField(lockDifficulty);
            fields.ServerField(lockFanciness);
        }
    };
} // namespace KCDC::Shared::Entities
