#pragma once

#include "shared/features/player/player_missile.h"

#include "shared/features/animation/animation_playback.h"
#include "shared/features/animation/face_state.h"
#include "shared/features/npc/npc_types.h"
#include "shared/features/player/player_appearance.h"
#include "shared/game/animal_catalog.h"
#include "shared/game/animation_tags.h"

#include <networking/replication/delegation.h>
#include <networking/replication/nametag_state.h>
#include <networking/replication/network_entity.h>

#include <mafianet/string.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace KCDC::Shared::Entities {
    /**
     * An NPC the server owns: a guard, a merchant, a bandit. Not one of the
     * level's own population, and not a player's body.
     *
     * The server decides everything about it and can simulate none of it -- it
     * has no navmesh, no Mannequin, no soul runtime -- so one client is elected
     * to run the body and everyone else draws what that client reports. The
     * election is the framework's (`DelegationManager`), which makes the elected
     * client this entity's *owner*: its pose travels upstream and is relayed,
     * and the server's decisions reach it on the forced-state channel.
     *
     * Two origins share one entity. A **spawned** NPC names a `soulGuid` and
     * every client makes a body for it. An **adopted** one names `levelGuid`,
     * the `EntityGuid` of a body the level already placed -- the same number on
     * every machine, which is how doors, gates and stashes are already
     * addressed.
     */
    class NpcEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Npc";

        // The quantization grid every body in this world shares. A guard and
        // the player talking to it must not disagree about where either is.
        static constexpr float kWorldMin        = -16384.0F;
        static constexpr float kWorldMax        = 16384.0F;
        static constexpr int kPositionBits      = 24;
        static constexpr float kMaxSpeed        = 128.0F;
        static constexpr int kRotationBits      = 16;
        static constexpr int kLookDirectionBits = 12;

        /** An empty tag group; the engine's own id for "nothing set". */
        static constexpr std::uint8_t kNoTag = Shared::Game::kNoAnimationTag;

        /**
         * How far an NPC streams. Below a player (250) and level with a door:
         * an NPC is scenery until somebody comes near enough to talk to it, and
         * every streamed body costs its client a spawn and an AI LOD slot.
         */
        static constexpr float kStreamRange = 150.0F;

        /** `extraGuards_dummy_1`, the fallback for a spawn that names no soul. */
        static constexpr const char *kDefaultSoulGuid = "63daa80b-d1f4-46f5-8222-c22a541cae07";

        // --- Construction snapshot: what this NPC *is* ------------------------

        /** The soul the body is spawned against. Empty for an adopted body. */
        std::string soulGuid;

        /** Level `EntityGuid` of the body this NPC adopts, or 0 when spawned. */
        std::uint64_t levelGuid = 0;

        /** `NPC`, `NPC_Female`, or an animal class. Empty means `NPC`. */
        std::string actorClass;

        bool IsAnimal() const {
            return Shared::Game::IsAnimalClass(actorClass);
        }

        /**
         * Bumped whenever the body must be made again from scratch -- a
         * respawn, a soul change. The pose channel echoes it, so a pose
         * captured against the previous body cannot snap the new one, which is
         * the same fence `HumanEntity::poseIncarnation` is. It rides with the
         * server-authored block below rather than only at construction: every
         * combat report about the NPC is fenced on it, so a client still on
         * the old value has each blow by or on a revived body refused.
         */
        std::uint32_t spawnEpoch = 1;

        // --- Server-authored state -------------------------------------------
        //
        // Every field in this block is written by the server and by nobody
        // else, and every one of them rides BOTH the field channel (so
        // observers have it) and the forced-state channel (so the simulator,
        // whom the field channel is withheld from, has it too).
        //
        // The simulator echoes them back in its own updates, which is only safe
        // because a server write is always followed by ForceState(): that bumps
        // the state epoch, and an echo carrying the old epoch is dropped rather
        // than reverting the write. `Npcs::` is the one place that writes these,
        // and it is the one place that has to remember it.

        /** What every client shows over the body; empty keeps the soul's own name. */
        std::string name;

        /** The four component names that make a KCD2 body, or empty to leave the soul's. */
        Shared::Game::CharacterAppearance appearance;

        /** What it wears, as item class GUIDs back to back -- `HumanEntity::equipment`'s format. */
        std::string equipment;

        /** Authoritative stock and worn row ids, retained across body rebuilds. */
        std::string inventoryState;

        /** Faction row for crime and relationship decisions; 0 is "no faction". */
        std::uint8_t factionId = 0;

        Shared::Game::NpcFlags flags = Shared::Game::NpcFlags::None;

        /** Which way the simulator moves this body. See `NpcLocomotion`. */
        Shared::Game::NpcLocomotion locomotion = Shared::Game::NpcLocomotion::Native;

        /**
         * The server's ledger of this body's health. The server is authoritative
         * because damage arrives from attackers' clients and has to be arbitrated
         * somewhere -- the same place PvP already arbitrates it.
         */
        float health    = 100.0F;
        float maxHealth = 100.0F;

        /** Cleared by the server when health reaches zero; a corpse is still an entity. */
        bool alive = true;

        // Simulator-authored durable combat pose; fenced to the current order.
        bool combatStance                 = false;
        std::uint8_t combatGuard          = 0;
        std::int32_t combatZone           = -1;
        std::uint32_t combatStateRevision = 0;

        Shared::Game::NpcIntent intent;

        /**
         * What the server is making this body play, on the simulator and on
         * every observer alike: a Mannequin action is local to each machine,
         * so each one plays it on its own copy.
         */
        Shared::Game::AnimationPlayback animation;

        /**
         * The `animation` revision whose end the server has told scripts
         * about, so each request raises `npcAnimationEnd` once however it
         * ended. Server bookkeeping; never serialized.
         */
        std::uint32_t animationEndAnnounced = 0;

        /** What the server is making this body's face do, on the simulator and every observer alike. */
        Shared::Game::FaceState face;

        /**
         * Bumped on every intent change. The simulator reports progress against
         * the revision it was running, so a report about the previous order
         * cannot be read as the answer to this one.
         */
        std::uint32_t intentRevision = 1;

        /**
         * Bumped by every scripted teleport, and by nothing else.
         *
         * The forced-state channel always carries the transform, because it
         * is the only way a teleport reaches the simulator -- but it is also
         * how every intent revision reaches it, and the transform in those is
         * only the server's copy of where the simulator last said the body
         * was. The simulator warps the body when this changes, never merely
         * because a forced state arrived.
         */
        std::uint32_t teleportSequence = 0;

        /** NetworkID of the player whose client is simulating this NPC, 0 while dormant. */
        std::uint64_t simulatorNetworkId = 0;

        /**
         * What the tag over the body draws: which parts, in what colour, and
         * any text standing in for `name`. Server-authored like the rest of
         * the block it rides in, so the simulator is told through the forced
         * state; whether a tag is drawn at all is still the `ShowNametag` flag.
         */
        Framework::Networking::Replication::NametagState nametag;

        // --- Simulator report -------------------------------------------------
        //
        // Measurements rather than decisions, written by the simulator alone.
        // The server reads the status pair as a report and decides what it
        // means for the order on its own side (`NpcOrder`), so a stale delta
        // from a simulator can never overwrite an outcome the server reached.
        // The one server write is `busy`, cleared at a handover, which the
        // next simulator then reports for itself.

        Shared::Game::NpcIntentStatus intentStatus = Shared::Game::NpcIntentStatus::Idle;

        /** Which `intentRevision` the status above is about. */
        std::uint32_t statusRevision = 0;

        /**
         * Whether the body is in the middle of something a handover cannot
         * continue: an attack, a climb, a shared animation. The server asks this
         * through `CanReleaseSimulation`, which is what makes the framework's
         * handover deadline mean something here.
         */
        bool busy = false;

        // --- Pose --------------------------------------------------------------

        /** Set once a simulator has published a pose; observers hold the spawn until then. */
        bool transformReady = false;

        /** Echoes `spawnEpoch`; a pose that does not is a pose for a body that is gone. */
        std::uint32_t poseEpoch = 0;

        std::uint8_t moveSpeedTag = Shared::Game::kNoAnimationTag;
        std::uint8_t moveDirTag   = Shared::Game::kNoAnimationTag;
        std::uint8_t stanceTag    = Shared::Game::kNoAnimationTag;

        /**
         * The simulating body's Mannequin tag state, whole.
         *
         * The three tags above name the locomotion groups and nothing else, so
         * everything the engine's own tag pass sets beside them -- the foot
         * phase it stops on, the stair and full-body tags, whatever a one-shot
         * left behind -- would be lost on an observer and the body would read
         * subtly differently on each machine. Twenty delta-coded bytes that send
         * nothing while the tags hold, which is nearly always.
         */
        static constexpr std::size_t kTagStateBytes = 20;
        std::uint8_t tagState[kTagStateBytes]       = {};
        bool inAir                                  = false;
        Shared::RPC::MissileState missile;

        bool headLookReady          = false;
        glm::vec3 headLookDirection = glm::vec3(0.0F, 1.0F, 0.0F);

        // --- Delegation ---------------------------------------------------------

        /**
         * Who may run this body, and from how far.
         *
         * 120 m to take it on and 160 m to keep it: an NPC is streamed at 150,
         * so a simulator holds its body until a little past the point where it
         * would stop being able to see it, and takes one on well inside that.
         *
         * The weight is 1 and the budget is set per client by the server
         * (`NpcService`), because the real ceiling is not bandwidth but the
         * game's own AI LOD Detail budget of 70 bodies -- shared with the
         * level's own population.
         */
        static constexpr Framework::Networking::Replication::DelegationPolicy kDelegationPolicy {
            /* acquireRange */ 120.0F,
            /* releaseRange */ 160.0F,
            /* weight */ 1,
            /* graceMs */ 5000,
            /* requireSameVirtualWorld */ true,
        };

        const Framework::Networking::Replication::DelegationPolicy *GetDelegationPolicy() const override {
            return &kDelegationPolicy;
        }

        bool CanReleaseSimulation() const override {
            return !busy;
        }

        void OnSimulatorChanged(MafiaNet::PeerGuid previous, MafiaNet::PeerGuid current) override {
            (void)previous;
            (void)current;
            // The new simulator re-derives from the intent rather than resuming
            // the old one's progress, so the report that belonged to the peer
            // that had it must not be read as this one's answer. Bumping the
            // revision here -- inside the framework's handover, before the
            // forced state goes out -- is what makes the two agree: a report
            // still carrying the old revision no longer matches.
            intentRevision = AdvanceRevision(intentRevision);
            busy           = false;
        }

        Framework::Networking::Replication::NametagState *GetNametag() override {
            return &nametag;
        }

        // --- Wire ---------------------------------------------------------------

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            MafiaNet::RakString wireSoul(soulGuid.c_str());
            MafiaNet::RakString wireClass(actorClass.c_str());
            fields.Field(wireSoul);
            fields.Field(wireClass);
            fields.Field(levelGuid);
            if (!fields.Writing()) {
                soulGuid   = wireSoul.C_String();
                actorClass = wireClass.C_String();
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeServerState(fields);

            // Send status and revision as one delta. A new order can finish
            // with the same status as the last, and the pair has to arrive
            // together for the server to read the status against the right
            // revision.
            std::uint64_t wireReport = (static_cast<std::uint64_t>(statusRevision) << 8U) | static_cast<std::uint8_t>(intentStatus);
            fields.Field(wireReport);
            fields.Field(busy);
            fields.Field(combatStance);
            fields.Field(combatGuard);
            fields.Field(combatZone);
            fields.Field(combatStateRevision);
            missile.SerializeFields(fields);
            if (!fields.Writing()) {
                const auto wireStatus = static_cast<std::uint8_t>(wireReport);
                intentStatus          = wireStatus < static_cast<std::uint8_t>(Shared::Game::NpcIntentStatus::Count) ? static_cast<Shared::Game::NpcIntentStatus>(wireStatus) : Shared::Game::NpcIntentStatus::Idle;
                statusRevision        = static_cast<std::uint32_t>(wireReport >> 8U);
            }
        }

        /**
         * The server's override of an NPC a client is simulating.
         *
         * This is not an extra: it is the only channel that reaches the
         * simulator at all, because the field channel is withheld from an
         * entity's owner. Everything the server decides is here.
         */
        void SerializeForcedState(Framework::Networking::Replication::FieldSerializer &fields) override {
            Framework::Networking::Replication::NetworkEntity::SerializeForcedState(fields);
            SerializeServerState(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(poseEpoch);
            fields.Field(transformReady);
            if (!transformReady) {
                return;
            }

            fields.FloatBits(position.x, kWorldMin, kWorldMax, kPositionBits);
            fields.FloatBits(position.y, kWorldMin, kWorldMax, kPositionBits);
            fields.FloatBits(position.z, kWorldMin, kWorldMax, kPositionBits);

            fields.Float16(velocity.x, -kMaxSpeed, kMaxSpeed);
            fields.Float16(velocity.y, -kMaxSpeed, kMaxSpeed);
            fields.Float16(velocity.z, -kMaxSpeed, kMaxSpeed);

            fields.FloatBits(rotation.x, -1.0F, 1.0F, kRotationBits);
            fields.FloatBits(rotation.y, -1.0F, 1.0F, kRotationBits);
            fields.FloatBits(rotation.z, -1.0F, 1.0F, kRotationBits);
            fields.FloatBits(rotation.w, -1.0F, 1.0F, kRotationBits);

            // Beside the pose, not only on the reliable channel: a pose paired
            // with tags from another frame is a body sliding through an idle.
            fields.Field(moveSpeedTag);
            fields.Field(moveDirTag);
            fields.Field(stanceTag);
            for (std::size_t i = 0; i < kTagStateBytes; ++i) {
                fields.Field(tagState[i]);
            }
            fields.Field(inAir);

            fields.Field(headLookReady);
            if (headLookReady) {
                fields.FloatBits(headLookDirection.x, -1.0F, 1.0F, kLookDirectionBits);
                fields.FloatBits(headLookDirection.y, -1.0F, 1.0F, kLookDirectionBits);
                fields.FloatBits(headLookDirection.z, -1.0F, 1.0F, kLookDirectionBits);
            }
        }

        /** Whether this NPC is drawn from a soul of its own rather than adopting a level body. */
        bool IsSpawned() const {
            return levelGuid == 0;
        }

        /** The entity class a spawn should use; `NPC` when nothing else was named. */
        const char *SpawnClass() const {
            return actorClass.empty() ? "NPC" : actorClass.c_str();
        }

        /** Humanoid navigation can work unlocked doors; animals must route around doorways. */
        bool CanOpenDoors() const {
            const std::string_view type = SpawnClass();
            return type == "NPC" || type == "NPC_Female";
        }

        static std::uint32_t AdvanceRevision(std::uint32_t revision) {
            return revision + 1 != 0 ? revision + 1 : 1;
        }

      private:
        /** The server-authored block, in one wire order shared by both channels that carry it. */
        void SerializeServerState(Framework::Networking::Replication::FieldSerializer &fields) {
            MafiaNet::RakString wireName(name.c_str());
            fields.Field(wireName);
            if (!fields.Writing()) {
                name = wireName.C_String();
            }

            appearance.Serialize(fields);
            fields.Field(equipment);
            fields.Field(inventoryState);
            fields.Field(factionId);

            auto wireFlags      = static_cast<std::uint16_t>(flags);
            auto wireLocomotion = static_cast<std::uint8_t>(locomotion);
            fields.Field(wireFlags);
            fields.Field(wireLocomotion);
            fields.Field(health);
            fields.Field(maxHealth);
            fields.Field(alive);
            fields.Field(spawnEpoch);
            intent.Serialize(fields);
            fields.Field(intentRevision);
            fields.Field(teleportSequence);
            animation.Serialize(fields);
            face.Serialize(fields);
            fields.Field(simulatorNetworkId);
            nametag.Serialize(fields);
            if (!fields.Writing()) {
                // A bit this build has no meaning for is dropped rather than
                // stored: it would otherwise be echoed back by a simulator and
                // outlive the peer that invented it.
                flags      = static_cast<Shared::Game::NpcFlags>(wireFlags & Shared::Game::kNpcFlagMask);
                locomotion = wireLocomotion < static_cast<std::uint8_t>(Shared::Game::NpcLocomotion::Count) ? static_cast<Shared::Game::NpcLocomotion>(wireLocomotion) : Shared::Game::NpcLocomotion::Native;
            }
        }
    };
} // namespace KCDC::Shared::Entities
