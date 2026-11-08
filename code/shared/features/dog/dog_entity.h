#pragma once

#include "shared/game/animation_tags.h"

#include <networking/replication/network_entity.h>

#include <mafianet/string.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Entities {
    /**
     * A replicated dog companion.
     *
     * Unlike a horse, a dog belongs to somebody from the moment it exists:
     * there is no riderless phase and no migration. Its player's client owns
     * it for its whole life and is the only peer that runs its brain -- the
     * game's objective planner, its interactor, its morale. Every other client
     * spawns the same body, possesses it onto that player's soul so the world
     * treats it as somebody's dog, and then switches its planner off and draws
     * the owner's pose.
     *
     * What travels is therefore the pose, plus a small declaration of what the
     * dog is doing, which receivers use to choose a presentation and never to
     * decide where the animal goes.
     */
    class DogEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Dog";

        // Matches HumanEntity and HorseEntity: a dog and the player it follows
        // are the same pose a frame apart, and two different grids would make
        // them disagree.
        static constexpr float kWorldMin   = -16384.0F;
        static constexpr float kWorldMax   = 16384.0F;
        static constexpr int kPositionBits = 24;
        static constexpr float kMaxSpeed   = 128.0F;
        static constexpr int kRotationBits = 16;

        /**
         * `animal_dog`, the generic dog of the game's soul tables, and the
         * parallel of `HorseEntity::kDefaultSoulGuid`. What a spawn falls back
         * to when the caller names no soul.
         */
        static constexpr const char *kDefaultSoulGuid = "fcfe02fe-7fcf-4919-b00e-193f5006dde7";

        /** `E_DogObjective::Invalid`, the 26th value: no objective running. */
        static constexpr std::uint8_t kNoObjective = 25;

        /** `E_DogCompanionMode::Follow`, what a new dog starts on. */
        static constexpr std::uint8_t kDefaultMode = 1;

        /** An empty tag group; the engine's own id for "nothing set". */
        static constexpr std::uint8_t kNoTag = Shared::Game::kNoAnimationTag;

        // --- Behaviour flags, the presentation half of the intent channel ---
        static constexpr std::uint8_t kFlagBarking  = 1U << 0U;
        static constexpr std::uint8_t kFlagCarrying = 1U << 1U;
        static constexpr std::uint8_t kFlagInCombat = 1U << 2U;
        static constexpr std::uint8_t kFlagDisobeys = 1U << 3U;

        /**
         * The soul the body is spawned against -- its identity and its coat.
         * Immutable, so it rides the construction snapshot and a late-joining
         * client spawns the same animal everyone else is looking at.
         */
        std::string soulGuid;

        /**
         * The name every client shows for this dog; empty leaves the name its
         * soul was born with alone.
         *
         * Server-authoritative, and on the forced-state channel for the same
         * reason `ownerNetworkId` is -- see below.
         */
        std::string name;

        /**
         * NetworkID of the HumanEntity this dog belongs to, 0 while it has no
         * master.
         *
         * Server-authoritative, and every client possesses the body onto that
         * player's soul from it. It rides the forced-state channel because the
         * field channel is withheld from an entity's owner, and the owner here
         * is the dog's own player -- who would otherwise hold a stale value,
         * echo it back, and clear the server's record of who owns the dog.
         */
        std::uint64_t ownerNetworkId = 0;

        /**
         * Whether the dog goes with its owner when they disconnect. Cleared by
         * a script that keeps dogs in the world between sessions; the dog is
         * then left masterless instead, and with no owner's client to run its
         * brain it stands where it was. Server-only, not serialized.
         */
        bool destroyWithOwner = true;

        /**
         * `E_DogCompanionMode`: Wait, Follow, Free, Aggressive, Search, Hunt,
         * Guard, Ambush.
         *
         * Server-authoritative and durable -- singleplayer chunk-saves it, so
         * it is real state rather than a transient. Forced for the same reason
         * as `ownerNetworkId`.
         */
        std::uint8_t mode = kDefaultMode;

        // --- Intent: what the owner says the dog is doing -------------------
        //
        // The owner publishes the dog's current objective. Receivers use
        // it to choose a presentation; the transform channel carries its
        // position independently.

        /** `E_DogObjective` of the running objective, or `kNoObjective`. */
        std::uint8_t objective = kNoObjective;

        /** NetworkID of the objective's target, 0 when it has no entity target. */
        std::uint64_t objectiveTargetId = 0;

        /** Presentation flags; see the `kFlag*` constants. */
        std::uint8_t behaviourFlags = 0;

        /**
         * The dog's morale, as the owner last read it off derived stat 42.
         *
         * A report, not a replicated simulation: morale advances from the
         * local frame delta through the OwnedDog buff's soul modifiers, and
         * there is no clock a server could drive that from. Observers display
         * it; they never age it.
         */
        float morale = 0.0F;

        /**
         * The locomotion tags the owner's engine picked this frame.
         *
         * As for a horse: KCD2 chooses a Mannequin fragment from the tag
         * state, so a walk, a trot and a run are one small number rather than
         * something a receiver could re-derive from apparent speed.
         */
        std::uint8_t moveSpeedTag = kNoTag;
        std::uint8_t moveDirTag   = kNoTag;
        std::uint8_t stanceTag    = kNoTag;

        /** Bytes of the Mannequin tag state; `CTagState::kStateBytes`. */
        static constexpr std::size_t kTagStateBytes = 20;

        /**
         * The owner's whole Mannequin tag state.
         *
         * The three tags above are the animal's pace. The other groups --
         * Mood, Variations, and the rest of Stance (`growling`, `sniffing`,
         * `Lying`, `Sleeping`) -- carry everything else, and travel whole
         * because nothing else on the wire names them. Twenty delta-coded
         * bytes that send nothing while the tags hold, which is nearly always.
         */
        std::uint8_t tagState[kTagStateBytes] = {};

        /** A dog in view must bring the player it belongs to with it. */
        Framework::Networking::Replication::NetworkEntity *GetInterestDependency() override {
            return ownerNetworkId != 0 ? ResolveSibling(ownerNetworkId) : nullptr;
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            MafiaNet::RakString wireSoul(soulGuid.c_str());
            MafiaNet::RakString wireName(name.c_str());
            fields.Field(wireSoul);
            fields.Field(wireName);
            fields.Field(ownerNetworkId);
            fields.Field(mode);
            if (!fields.Writing()) {
                soulGuid = wireSoul.C_String();
                name     = wireName.C_String();
            }
        }

        // Delta-coded state channel: reliable, and sent in full on the first
        // update to a fresh peer.
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            MafiaNet::RakString wireName(name.c_str());
            fields.Field(wireName);
            if (!fields.Writing()) {
                name = wireName.C_String();
            }
            fields.Field(ownerNetworkId);
            fields.Field(mode);
            fields.Field(objective);
            fields.Field(objectiveTargetId);
            fields.Field(behaviourFlags);
            fields.Field(morale);
            fields.Field(moveSpeedTag);
            fields.Field(moveDirTag);
            fields.Field(stanceTag);
            for (std::size_t i = 0; i < kTagStateBytes; ++i) {
                fields.Field(tagState[i]);
            }
        }

        /**
         * The server's override of a dog its player owns.
         *
         * The base half carries the pose; the owner, name and mode are
         * appended so the owning client adopts the server's values rather than
         * the ones it happened to hold. Unlike a horse, nothing here arms a
         * warp: the server never moves a dog, so `Dog::OnStateForced` adopts
         * the fields and leaves the animal where its own engine put it.
         */
        void SerializeForcedState(Framework::Networking::Replication::FieldSerializer &fields) override {
            Framework::Networking::Replication::NetworkEntity::SerializeForcedState(fields);
            fields.Field(ownerNetworkId);
            fields.Field(mode);

            MafiaNet::RakString wireName(name.c_str());
            fields.Field(wireName);
            if (!fields.Writing()) {
                name = wireName.C_String();
            }
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, kWorldMin, kWorldMax, kPositionBits);
            fields.FloatBits(position.y, kWorldMin, kWorldMax, kPositionBits);
            fields.FloatBits(position.z, kWorldMin, kWorldMax, kPositionBits);

            // Carried so receivers can extrapolate between updates, which is
            // what keeps a running dog from stepping at the tick rate.
            fields.Float16(velocity.x, -kMaxSpeed, kMaxSpeed);
            fields.Float16(velocity.y, -kMaxSpeed, kMaxSpeed);
            fields.Float16(velocity.z, -kMaxSpeed, kMaxSpeed);

            fields.FloatBits(rotation.x, -1.0F, 1.0F, kRotationBits);
            fields.FloatBits(rotation.y, -1.0F, 1.0F, kRotationBits);
            fields.FloatBits(rotation.z, -1.0F, 1.0F, kRotationBits);
            fields.FloatBits(rotation.w, -1.0F, 1.0F, kRotationBits);
        }
    };
} // namespace KCDC::Shared::Entities
