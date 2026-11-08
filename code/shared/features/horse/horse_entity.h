#pragma once

#include "shared/features/npc/npc_types.h"

#include <networking/replication/delegation.h>
#include <networking/replication/network_entity.h>

#include <mafianet/string.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Entities {
    /**
     * A replicated horse.
     *
     * Server-spawned, unlike a player's body: a horse exists because a script
     * or a command asked for it. Some client always runs it with the real
     * engine: whoever is in the saddle, pinned there for the length of the
     * ride, and otherwise the nearest player, elected by the framework's
     * delegation. That client is the entity's owner, so its game is the one
     * copy whose soul takes damage and whose death is the horse's death; every
     * other copy only reproduces what it reports.
     *
     * One peer drives the mount and the other peers follow its state.
     */
    class HorseEntity : public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Horse";

        // Quantization matches HumanEntity: a horse and its rider are the same
        // pose a frame apart, and two different grids would make them disagree.
        static constexpr float kWorldMin   = -16384.0F;
        static constexpr float kWorldMax   = 16384.0F;
        static constexpr int kPositionBits = 24;
        static constexpr float kMaxSpeed   = 128.0F;
        static constexpr int kRotationBits = 16;

        /**
         * `animal_horse`, the generic riding horse of the game's soul tables.
         * What a spawn falls back to when the caller names no soul.
         */
        static constexpr const char *kDefaultSoulGuid = "fc0e6251-b314-4398-b83f-3a66a52961ee";

        /**
         * The soul the body is spawned against -- its identity and its coat.
         * Immutable, so it rides the construction snapshot and a late-joining
         * client spawns the same animal everyone else is looking at.
         */
        std::string soulGuid;

        /**
         * The name every client shows for this animal; empty leaves the name
         * its soul was born with alone.
         *
         * Server-authoritative and the one horse field a script may write.
         * Rides the construction snapshot so a late joiner spawns the body
         * already named, and reaches the simulator on the forced-state
         * channel, like every server field -- see `riderNetworkId`.
         */
        std::string name;

        /**
         * What this horse can carry, in the engine's own weight units, or 0
         * before any client has said.
         *
         * The engine recomputes it from the soul, its buffs and its saddlebags
         * on every read, so only a peer running the game can answer it: the
         * simulator reports it and the server holds the answer.
         */
        float inventoryCapacity = 0.0F;

        // --- Gear ---------------------------------------------------------------
        //
        // What the horse wears: saddle, bridle or chanfron, caparison and
        // shoes, the four slots of the game's horse inventory screen. Gear is
        // clothing to the engine -- `<Armor>` worn through the soul's own
        // equipment manager -- so a copy that is not told about it spawns the
        // bare animal its soul describes, and the simulator runs a horse
        // without the saddle's carrying capacity or the caparison's armour.
        //
        // A server verdict, like the life fields: a client reports a change
        // (`HorseGearReport`), the server checks it, and every write bumps the
        // revision and forces the state so the simulator hears it too.

        /**
         * The worn item classes, packed as `Shared::Game::PackHorseGear` writes
         * them: 32 hex digits a class, in slot order. Empty is a bare horse.
         * A spawned soul brings no tack of its own -- the one outfit the
         * engine would apply is refused (`HorseService::ClaimGear`) -- so the
         * server dresses a new horse the way the game dresses its own.
         */
        std::string gear;

        /** Bumped by the server on every gear write; 0 until the first one, which is a bare horse. */
        std::uint32_t gearRevision = 0;

        /** Whether a script has frozen the gear against players' changes. Server-only, not serialized. */
        bool gearLocked = false;

        // --- Buffs ----------------------------------------------------------------
        //
        // A horse's stats are its soul's derived stats -- speed is
        // `RunSpeedBase`, with stamina, courage and carrying capacity beside
        // it -- and the game itself moves them with buffs: horseshoes carry
        // `rsb+0.04`, the race boosts `mst+30,rms+0.01`. So a server changes a
        // horse's stats the way the game does, by naming buffs. Unlike a
        // player's, which only the owning body wears, these are state: every
        // copy of the horse puts them on its own soul, so the simulator's
        // speed, a late joiner's copy and a copy whose owner changed all agree.

        /**
         * The buff definitions the server put on this horse, as canonical
         * GUID text (`8-4-4-4-12`) joined by ','. Empty is a horse wearing
         * only what the game gave it.
         */
        std::string buffs;

        /** Bumped by the server on every buff write; 0 until the first one. */
        std::uint32_t buffRevision = 0;

        /** The most buffs a server may hold on one horse. The wire string is sized from it. */
        static constexpr std::size_t kMaxBuffs = 16;

        /**
         * NetworkID of the HumanEntity in the saddle, 0 when nobody rides it.
         *
         * A server field: never taken from the simulator's upstream stream,
         * so the rider who owns the horse cannot echo a stale seat back over
         * the server's record. The field channel is withheld from an owner,
         * so the simulator learns it from the forced state instead.
         */
        std::uint64_t riderNetworkId = 0;

        /**
         * Whether the rider was put straight into the saddle rather than
         * climbing on: a script that asked for it (`player.mount` with
         * `instant`), as against a player's own mount or a script's animated
         * one. A server field beside `riderNetworkId`, written with it from
         * the rider's own report of how its game seated it. A copy that
         * watches the seat fill plays the get-on only for a mount that was
         * not instant; one that streams in on a ride already under way seats
         * its rider either way.
         */
        bool seatedInstantly = false;

        /**
         * The flank the last rider got off on, 1 or 2, from the rider's own
         * report; 0 when the server took them off. Written with the empty
         * seat, so a copy reads it the moment it starts its get-off.
         */
        std::uint8_t dismountSide = 0;

        /**
         * NetworkID of the HumanEntity this horse belongs to, 0 while it is
         * nobody's.
         *
         * Not the rider and not the simulator: a horse can belong to one
         * player, carry another and be run by a third. Every client possesses
         * the body onto the owner's soul from it, which is the game's own
         * notion of whose horse this is -- the one its horse-theft checks and
         * its horse inventory screen consult.
         *
         * A server field, for the reason `riderNetworkId` is: the simulator
         * never hears the field channel, so the forced state carries it there.
         */
        std::uint64_t ownerNetworkId = 0;

        /**
         * Whether the horse goes with its owner when they disconnect. Cleared
         * by a script that keeps horses in the world between sessions; the
         * horse is then left with no owner instead. Server-only, not
         * serialized.
         */
        bool destroyWithOwner = true;

        // --- Life ---------------------------------------------------------------
        //
        // Two kinds of field. Health and stamina are measurements: a horse's
        // health lives on its soul, only a peer running the game has one, and
        // the simulator reports them upstream. Whether the horse is alive, who
        // killed it and `vitalsRevision` are the server's verdicts: server
        // fields the simulator never sends, reached from its `HorseDied`
        // report or a script's say-so. Every verdict bumps the revision and
        // forces the state, and the simulator applies a revision it has not
        // seen to its own soul.
        //
        // The split is what keeps a death from being undone. A forced state
        // pushed for something else -- the rider getting off in the same
        // instant -- carries the server's verdict of that moment, and a
        // simulator that echoed it back would revive the horse everywhere.

        /** Current and maximum health, or both 0 before any simulator has reported. */
        float health    = 0.0F;
        float maxHealth = 0.0F;

        /** Current and maximum stamina -- the one bar the game's HUD draws for a horse. */
        float stamina    = 0.0F;
        float maxStamina = 0.0F;

        /**
         * Whether the simulator's soul is unconscious: alive, but knocked out.
         * A measurement like health. The game knocks out instead of killing a
         * soul it holds immortal, which a player's own horse is. A new
         * simulator is handed it in the forced state and keeps the horse as
         * it says.
         */
        bool unconscious = false;

        /**
         * Cleared by the server when the simulator reports its soul died or a
         * script kills it, and set again only by a revival. A corpse is still
         * an entity: it stays until a script revives or destroys it.
         */
        bool alive = true;

        /**
         * Who took the last of the health and why, as the simulator saw it:
         * a player's NetworkID or 0, and a `Game::DamageReason`. Meaningful
         * only while `alive` is false.
         */
        std::uint64_t killerNetworkId = 0;
        std::uint8_t deathReason      = 0;

        /** Bumped by the server on every life write it makes; see above. */
        std::uint32_t vitalsRevision = 0;

        /** Whether `horseDeath` has been raised for this death. Server-only, not serialized. */
        bool deathAnnounced = false;

        static std::uint32_t AdvanceRevision(std::uint32_t revision) {
            return revision + 1 != 0 ? revision + 1 : 1;
        }

        // --- Delegation ---------------------------------------------------------

        /**
         * Who runs a riderless horse: the nearest player within 80 m, kept to
         * 110 m. Inside the 250 m a player streams at, so the elected client
         * has the body before it is asked to run it.
         *
         * Weight 0, because a horse costs no AI LOD slot -- its AI is refused
         * at creation -- and so must not eat into the NPC budget the per-client
         * capacity is set for. A ridden horse is pinned to its rider instead.
         */
        static constexpr Framework::Networking::Replication::DelegationPolicy kDelegationPolicy {
            /* acquireRange */ 80.0F,
            /* releaseRange */ 110.0F,
            /* weight */ 0,
            /* graceMs */ 5000,
            /* requireSameVirtualWorld */ true,
        };

        const Framework::Networking::Replication::DelegationPolicy *GetDelegationPolicy() const override {
            return &kDelegationPolicy;
        }

        /**
         * Not while it is knocked out, for the policy's grace at most, as a
         * busy NPC is kept. A downed horse lies on every machine where that
         * machine's own body fell -- a knocked-out copy is never placed from
         * the wire -- and the position the election measures is the one its
         * simulator reports. Handing it over moves that position to another
         * machine's body, and the next pass could hand it straight back: two
         * players beside a downed horse traded it every 500 ms.
         */
        bool CanReleaseSimulation() const override {
            return !unconscious;
        }

        /** A horse in view must bring whoever is in the saddle with it. */
        Framework::Networking::Replication::NetworkEntity *GetInterestDependency() override {
            return riderNetworkId != 0 ? ResolveSibling(riderNetworkId) : nullptr;
        }

        /**
         * Counts the jumps the rider has made the horse take, wrapping.
         *
         * The rider's own body, whose replicated input drives every copy
         * natively, steers the horse; the gait it commands and the jump are
         * carried here beside the pose, because the rider's packet and the
         * horse's are two streams, and a copy that takes its gait from one
         * and its speed from the other runs the wrong stride in between.
         * The jump is consumed by the horse rather than the body, so it is
         * counted where the horse's runtime commits it. A jump is an event, not a state a later packet
         * can restate, and it is not optional for the other copies: they walk
         * under their own physics, so a fence the rider's horse cleared stops
         * one that did not jump too. A count survives a packet that never
         * arrives; each copy jumps once for every step it sees.
         */
        std::uint8_t jumpSequence = 0;

        /**
         * The reactions the rider's horse made under them -- a quick start
         * (ride state 4), pulling up from a walk (5), a hard stop (6),
         * rearing (7), throwing them off (8) -- counted as the jumps are, and
         * the ride states of the newest kReactionHistory, four bits each with
         * the newest in the lowest. A copy does not decide these from its own
         * speed, fear and timing, which differ from the owner's; it plays
         * every step it sees, in order, so the animal starts off, stops,
         * rears or throws its rider on every machine when it does on the
         * owner's, even when several fall between two packets.
         */
        static constexpr std::uint8_t kReactionHistory = 8;
        std::uint8_t reactionSequence                  = 0;
        std::uint32_t reactions                        = 0;

        /**
         * The gait the rider's command asks the horse for, as the engine's
         * move style id plus one, so zero is the stand (-1). Carried on the
         * transform channel with the pose it belongs to, never a field: a
         * field is reliable and ordered, so after a loss it lands whole
         * round trips after the pose it was sampled with.
         */
        std::uint8_t gaitCommand = 0;

        /**
         * The pace the simulator's horse is actually playing, as the
         * pseudo speed `ApplyRuntimePaceAndDelta` (0x1806CF3D4) put on its
         * movement request: 0 standing, 1 walk, 2 trot, 3 canter, 4
         * gallop. It is what the MoveSpeed tag is resolved from
         * (`ResolveMoveSpeedTag`, 0x1807FEB60), so a copy told this plays
         * the owner's stride instead of selecting one from its measured
         * velocity. On the transform
         * channel with the pose for the reason `gaitCommand` is.
         */
        static constexpr std::uint8_t kMaxPace = 4;
        std::uint8_t pace                      = 0;

        Shared::Game::NpcIntent navigationIntent;
        std::uint32_t navigationRevision               = 0;
        Shared::Game::NpcIntentStatus navigationStatus = Shared::Game::NpcIntentStatus::Idle;
        std::uint32_t navigationStatusRevision         = 0;

        void SerializeNavigation(Framework::Networking::Replication::FieldSerializer &fields) {
            auto incoming = navigationIntent;
            incoming.Serialize(fields);
            fields.ServerField(navigationRevision);
            if (!fields.Writing() && fields.ServerAuthored()) {
                navigationIntent = incoming;
            }
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            MafiaNet::RakString wireSoul(soulGuid.c_str());
            MafiaNet::RakString wireName(name.c_str());
            fields.Field(wireSoul);
            fields.Field(wireName);
            fields.Field(inventoryCapacity);
            MafiaNet::RakString wireGear(gear.c_str());
            fields.Field(wireGear);
            fields.Field(gearRevision);
            fields.Field(ownerNetworkId);
            MafiaNet::RakString wireBuffs(buffs.c_str());
            fields.Field(wireBuffs);
            fields.Field(buffRevision);
            if (!fields.Writing()) {
                soulGuid = wireSoul.C_String();
                name     = wireName.C_String();
                gear     = wireGear.C_String();
                buffs    = wireBuffs.C_String();
            }
        }

        // Delta-coded state channel: reliable, and sent in full on the first
        // update to a fresh peer.
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeNavigation(fields);
            std::uint64_t report = (static_cast<std::uint64_t>(navigationStatusRevision) << 8U) | static_cast<std::uint8_t>(navigationStatus);
            fields.Field(report);
            if (!fields.Writing()) {
                const auto status        = static_cast<std::uint8_t>(report);
                navigationStatus         = status < static_cast<std::uint8_t>(Shared::Game::NpcIntentStatus::Count) ? static_cast<Shared::Game::NpcIntentStatus>(status) : Shared::Game::NpcIntentStatus::Idle;
                navigationStatusRevision = static_cast<std::uint32_t>(report >> 8U);
            }
            MafiaNet::RakString wireName(name.c_str());
            fields.ServerField(wireName);
            if (!fields.Writing() && fields.ServerAuthored()) {
                name = wireName.C_String();
            }
            fields.Field(inventoryCapacity);
            fields.ServerField(riderNetworkId);
            fields.ServerField(seatedInstantly);
            fields.ServerField(dismountSide);
            fields.ServerField(ownerNetworkId);
            fields.Field(jumpSequence);
            fields.Field(reactionSequence);
            fields.Field(reactions);
            fields.Field(health);
            fields.Field(maxHealth);
            fields.Field(stamina);
            fields.Field(maxStamina);
            fields.Field(unconscious);
            fields.ServerField(alive);
            fields.ServerField(killerNetworkId);
            fields.ServerField(deathReason);
            fields.ServerField(vitalsRevision);

            MafiaNet::RakString wireGear(gear.c_str());
            fields.ServerField(wireGear);
            if (!fields.Writing() && fields.ServerAuthored()) {
                gear = wireGear.C_String();
            }
            fields.ServerField(gearRevision);

            MafiaNet::RakString wireBuffs(buffs.c_str());
            fields.ServerField(wireBuffs);
            if (!fields.Writing() && fields.ServerAuthored()) {
                buffs = wireBuffs.C_String();
            }
            fields.ServerField(buffRevision);
        }

        /**
         * The server's override of the horse its simulator is running. The
         * base half carries the pose a scripted move travels on; the server
         * fields are appended because this is the only way the owner hears
         * them, and the measurements so a revision it applies starts from the
         * server's numbers rather than the ones it held when it took over.
         * Being forced, the measurements are also sent back in full once the
         * owner adopts them (`ReadForcedState` marks them changed): a new
         * simulator's delta history is the one it left with when it last ran
         * the horse, so a value equal to that was never reported, and the
         * server kept the previous simulator's.
         */
        void SerializeForcedState(Framework::Networking::Replication::FieldSerializer &fields) override {
            Framework::Networking::Replication::NetworkEntity::SerializeForcedState(fields);
            SerializeNavigation(fields);
            fields.Field(riderNetworkId);
            fields.Field(seatedInstantly);
            fields.Field(dismountSide);
            fields.Field(ownerNetworkId);

            MafiaNet::RakString wireName(name.c_str());
            fields.Field(wireName);
            if (!fields.Writing()) {
                name = wireName.C_String();
            }
            fields.Field(inventoryCapacity);
            fields.Field(health);
            fields.Field(maxHealth);
            fields.Field(stamina);
            fields.Field(maxStamina);
            fields.Field(unconscious);
            fields.Field(alive);
            fields.Field(killerNetworkId);
            fields.Field(deathReason);
            fields.Field(vitalsRevision);

            MafiaNet::RakString wireGear(gear.c_str());
            fields.Field(wireGear);
            if (!fields.Writing()) {
                gear = wireGear.C_String();
            }
            fields.Field(gearRevision);

            MafiaNet::RakString wireBuffs(buffs.c_str());
            fields.Field(wireBuffs);
            if (!fields.Writing()) {
                buffs = wireBuffs.C_String();
            }
            fields.Field(buffRevision);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, kWorldMin, kWorldMax, kPositionBits);
            fields.FloatBits(position.y, kWorldMin, kWorldMax, kPositionBits);
            fields.FloatBits(position.z, kWorldMin, kWorldMax, kPositionBits);

            // Carried so receivers can extrapolate between updates, which is
            // what keeps a galloping horse from stepping at the tick rate.
            fields.Float16(velocity.x, -kMaxSpeed, kMaxSpeed);
            fields.Float16(velocity.y, -kMaxSpeed, kMaxSpeed);
            fields.Float16(velocity.z, -kMaxSpeed, kMaxSpeed);

            fields.FloatBits(rotation.x, -1.0F, 1.0F, kRotationBits);
            fields.FloatBits(rotation.y, -1.0F, 1.0F, kRotationBits);
            fields.FloatBits(rotation.z, -1.0F, 1.0F, kRotationBits);
            fields.FloatBits(rotation.w, -1.0F, 1.0F, kRotationBits);

            // The stride beside the speed it goes with: six bits a pose.
            std::uint8_t wireCommand = std::min<std::uint8_t>(gaitCommand, kMaxPace);
            std::uint8_t wirePace    = std::min<std::uint8_t>(pace, kMaxPace);
            fields.BitsRange(wireCommand, std::uint8_t {0}, kMaxPace);
            fields.BitsRange(wirePace, std::uint8_t {0}, kMaxPace);
            if (!fields.Writing()) {
                gaitCommand = wireCommand;
                pace        = wirePace;
            }
        }
    };
} // namespace KCDC::Shared::Entities
