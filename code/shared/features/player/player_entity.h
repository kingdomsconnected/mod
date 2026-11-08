#pragma once

#include "shared/features/player/player_station.h"
#include "shared/features/player/station_sharpening.h"
#include "shared/features/dice/dice_scene.h"

#include "shared/features/animation/animation_playback.h"
#include "shared/features/animation/face_state.h"
#include "shared/features/player/player_action.h"
#include "shared/features/player/player_appearance.h"
#include "shared/features/player/player_disguise.h"
#include "shared/features/player/player_input.h"
#include "shared/features/player/player_missile.h"
#include "shared/features/player/player_progression.h"
#include "shared/features/player/player_soul_state.h"
#include "shared/game/animation_tags.h"
#include "shared/item_stack.h"

#include <networking/replication/nametag_state.h>
#include <networking/replication/network_entity.h>

#include <mafianet/string.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <logging/logger.h>
#include <numbers>
#include <string>
#include <tuple>

namespace KCDC::Shared::Entities {
    /**
     * A connected player, represented in the world by a KCD2 human actor.
     *
     * The owning client is authoritative for all of it -- body, RPG state,
     * equipment and pose -- because the server has no soul tables and no way
     * to evaluate what a stat or a skill is worth. Everyone else spawns a
     * native player body and replays the owner's input and state into it.
     */
    class HumanEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Human";

        // Transform quantization. KCD2's playable area sits well inside these
        // bounds, where 24 bits resolve to about 2 mm.
        static constexpr float kWorldMin   = -16384.0F;
        static constexpr float kWorldMax   = 16384.0F;
        static constexpr int kPositionBits = 24;
        static constexpr int kRotationBits = 16;

        // A body's position, over the ranges a level actually spans. Every
        // shipped terrain.dat is at most 4096 one-metre units square
        // (kutnohorsko and trosecko; klaster 2048) and peaks at 300 m, and
        // the placed content lies within 0-4036 m across and -7-249 m up.
        // Each axis keeps a level's width again around it, for a body
        // flying off the edge, at the same 2 mm a step as `kWorldMin`'s
        // 24 bits: 63 bits where those take 72.
        static constexpr float kPoseMinXY = -2048.0F;
        static constexpr float kPoseMaxXY = 6144.0F;
        static constexpr int kPoseBitsXY  = 22;
        static constexpr float kPoseMinZ  = -256.0F;
        static constexpr float kPoseMaxZ  = 768.0F;
        static constexpr int kPoseBitsZ   = 19;

        // A body's facing is one angle: the actor's rotation object rebuilds
        // its base orientation as a pure Z rotation (0x180441928), so the
        // other three quaternion components carry nothing. 12 bits resolve
        // 0.09 degrees.
        static constexpr int kYawBits = 12;

        // The head look as yaw and pitch, the way CryEngine's
        // SerializeDirHelper sends a look direction (pYaw/pElv): a unit
        // vector has two degrees of freedom, not three. About 0.7 degrees.
        static constexpr int kLookYawBits   = 9;
        static constexpr int kLookPitchBits = 8;

        // Travel in fixed steps around an exact zero, so a body at rest
        // encodes as rest. Horizontal covers a galloping horse's saddle at
        // 3 cm/s; vertical is read only to extrapolate a few tens of
        // milliseconds, and covers a long fall at 0.25 m/s.
        static constexpr float kMaxGroundSpeed   = 32.0F;
        static constexpr int kGroundSpeedBits    = 11;
        static constexpr float kMaxVerticalSpeed = 64.0F;
        static constexpr int kVerticalSpeedBits  = 9;

        // How many low bits of `poseIncarnation` the pose carries. The
        // receiver widens them against the soul it holds, which is never
        // more than a body or two away from the one a pose was taken of.
        static constexpr int kPoseIncarnationBits = 4;

        /** The yaw a body faces with `rotation`, CryEngine's forward being +Y. */
        static float YawOf(const glm::quat &rotation) {
            const glm::vec3 forward = rotation * glm::vec3(0.0F, 1.0F, 0.0F);
            return std::atan2(-forward.x, forward.y);
        }

        /** The upright rotation that faces `yaw`. */
        static glm::quat RotationForYaw(float yaw) {
            return glm::angleAxis(yaw, glm::vec3(0.0F, 0.0F, 1.0F));
        }

        std::string nickname;
        uint16_t playerIndex = 0xFFFF;

        /**
         * What everyone else draws over this body.
         *
         * Owner-applied like every other field here: a scripted change
         * reaches the owner as a SetNametagState RPC and travels on from
         * there, so one peer stays authoritative for the tag as for the pose.
         */
        Framework::Networking::Replication::NametagState nametag;

        /**
         * The RPG state of the owner's body, in native units.
         *
         * Replaces the health percent and stamina fraction this entity used
         * to carry: those were summaries a receiver could draw but not
         * evaluate, and the puppet needs the real answers to run a hit or a
         * draw against. Readiness lives here too -- `soul.valid` is what
         * remote clients gate their spawn on -- so the body and the numbers
         * that describe it can never be a frame apart.
         */
        Shared::Game::HumanSoulState soul;

        /**
         * The derived stats scripts read past the soul's gameplay conditions.
         *
         * Owner-authored like `soul` but not part of it: they gate nothing,
         * so they do not advance its revision, and each rides the state
         * channel as its own delta field. Meaningful only while `soul.valid`.
         */
        Shared::Game::PlayerStatReadings statReadings;

        /**
         * What the server is making this body play. Server-only: it rides the
         * relay to everyone else and never the owner's upstream, so the owner
         * cannot echo back a request the server has since replaced -- and the
         * owner, which is sent neither channel, hears it as `PlayerAnimation`.
         */
        Shared::Game::AnimationPlayback animation;

        /**
         * The `animation` revision whose end the server has told scripts
         * about, so each request raises `playerAnimationEnd` once however it
         * ended. Server bookkeeping; never serialized.
         */
        std::uint32_t animationEndAnnounced = 0;

        /**
         * What the server is making this body's face do. Server-only and told
         * to the owner as `PlayerFace`, for the reasons `animation` is.
         */
        Shared::Game::FaceState face;

        /**
         * The engine's own hand numbering, shared by `SetHandItem`,
         * `AttachItem` and the `HS_RIGHT`/`HS_LEFT` script constants.
         */
        static constexpr std::size_t kHandCount = 2;
        static constexpr std::size_t kRightHand = 0;
        static constexpr std::size_t kLeftHand  = 1;

        /** "Neither hand", for the lookups that can fail to find one. */
        static constexpr std::size_t kNoHand = kHandCount;

        /**
         * Class of the item drawn in each hand, invalid when empty.
         *
         * A class rather than a WUID: the receiver makes its own item of that
         * class, so no engine-local id crosses the wire. Weapon sets do not
         * travel -- the set is implied by which slot the class occupies. Nor
         * do holstered weapons: those arrive through `equipment` and hang in
         * their scabbard on their own.
         */
        Shared::RPC::ItemClassId handItemClass[kHandCount];

        /**
         * World-space direction selected by the owner's movement controller
         * for its eyes/head. Sent as a yaw and a pitch, 17 bits in a
         * transform update, and avoids streaming any skeleton or camera
         * state.
         */
        bool headLookReady          = false;
        glm::vec3 headLookDirection = glm::vec3(0.0F, 1.0F, 0.0F);

        /**
         * What the owner is wearing, as its item classes back to back.
         *
         * A soul carries identity and appearance but no clothes: an actor
         * wears what its inventory holds and its equipment manager has
         * equipped, so the outfit travels separately. Each class is the 16
         * bytes of its GUID written as 32 hex digits, which needs no separator
         * and no parsing beyond a fixed stride. This is what the owner
         * actually has on rather than a preset picked from a table, so a bare
         * body replicates as a bare body and taking something off is a change
         * like any other.
         */
        std::string equipment;

        /**
         * How worn each piece in `equipment` is, in the same order: its dirt,
         * its six blood zones and its quality as two hex digits each, then the
         * bits of its health as eight -- 24 digits a piece, zeros for what is
         * not armour.
         *
         * A receiver makes its own items of those classes, and its game keeps
         * their dirt, blood and health itself, so without this every copy
         * draws the body's clothes as its own game has them rather than as the
         * owner's are. Dirt and blood are drawn from the armour's own state,
         * and the scratches from its condition: its health within the range
         * its quality allows.
         */
        std::string equipmentWear;

        /** An equipped visor is closed; drives remote helmet visuals and voice. */
        bool visorClosed = false;

        /**
         * The falls the owner's body took -- the game's `Fall`, which drops it
         * into a ragdoll, as when its horse strikes a wall -- counted, and where
         * the newest one struck, from where the body stood. A fall is an event,
         * so every copy plays one for each step it sees, once it is drawn where
         * its owner fell, struck at the same point from where it stands then;
         * a copy never takes one of its own (NativePlayerModule::ActorFall).
         */
        std::uint8_t fallSequence = 0;
        float fallHitX            = 0.0F;
        float fallHitY            = 0.0F;
        float fallHitZ            = 0.0F;

        /**
         * The soul half of what `equipment` is the clothes half: the four
         * component names that make a KCD2 body. A receiver writes them into
         * the puppet's look-as layer, which is what `C_Actor::MakeLookAsSoul`
         * does with a donor soul there is none of here.
         *
         * Owner-published, but unlike the pose it is a choice rather than a
         * measurement, and the server is where it is made: a scripted change
         * reaches the owner as a `SetPlayerAppearance` RPC and travels on
         * from there. Empty until somebody chooses, and an empty appearance
         * applies nothing.
         */
        Shared::Game::CharacterAppearance appearance;

        /**
         * The mesh this body is drawn as instead of itself, or none.
         *
         * Chosen by the server and published by the owner, exactly as
         * `appearance` is: a scripted change reaches the owner as a
         * `SetPlayerDisguise` RPC, the owner puts it on its own body and
         * republishes it, and everyone else draws the same on their puppet.
         */
        Shared::Game::PlayerDisguise disguise;

        /**
         * This body is not drawn anywhere: not on its owner's screen, not on
         * anyone else's.
         *
         * Decided by the server and published by the owner, like `disguise`:
         * a scripted change reaches the owner as a `SetPlayerVisible` RPC,
         * the owner stops drawing its own body and republishes this, and
         * every copy stops drawing in turn.
         *
         * Only the drawing stops. The body keeps its place, its collider, its
         * soul and its weapons, so it still blocks a doorway, still takes a
         * hit, and is still what an NPC sees -- and it takes what the body
         * carries with it, which is the half a character's own render gate
         * does not reach.
         */
        bool bodyHidden = false;

        /**
         * Set once the owner has captured a pose. Remote clients hold the
         * spawn until then, so the body appears where the player is rather
         * than at the origin.
         */
        bool transformReady = false;

        /**
         * How far a player is streamed once there is a pose to stream from.
         * Above the stash range (200) so a person is never culled before the
         * furniture around them.
         */
        static constexpr float kStreamRange = 250.0F;

        /**
         * Which body this pose belongs to, echoing `soul.incarnation`.
         *
         * The pose rides an unreliable channel and the soul a reliable one,
         * so a pose captured against the previous body can land after its
         * replacement is already published. Carrying the incarnation lets the
         * receiver drop that pose rather than snap the new body to where the
         * old one stood.
         *
         * The pose carries only its low `kPoseIncarnationBits`, widened on
         * receipt against `soul.incarnation`. Construction reads the pose
         * before the soul it would widen against, so it carries the whole
         * value as well, here, after the pose.
         */
        std::uint32_t poseIncarnation = 0;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            if (!fields.Writing()) {
                _wireSoul        = {};
                _soulStreamValid = true;
            }
            MafiaNet::RakString wireNickname(nickname.c_str());
            fields.Field(wireNickname);
            fields.Field(playerIndex);
            fields.Field(poseIncarnation);
            if (!fields.Writing()) {
                nickname = wireNickname.C_String();
            }
        }

        /** An empty tag group; the engine's own id for "nothing set". */
        static constexpr std::uint8_t kNoTag = Shared::Game::kNoAnimationTag;

        // The animation tags this machine's engine picked for the body it
        // draws, read back each frame for client scripts. Every client
        // animates every body itself from the replicated input, so these are
        // local observations: not serialized, and empty on the server.
        std::uint8_t moveSpeedTag = kNoTag;
        std::uint8_t moveDirTag   = kNoTag;
        std::uint8_t stanceTag    = kNoTag;

        /**
         * Whether the owner's body is sitting or lying, and on what, as its
         * own actor model records it. A seated body plays `SittingIdle`
         * through the native action and seat, rather than a locomotion tag. See
         * `BodyStanceState`.
         */
        Shared::Game::BodyStanceState bodyStance;

        Shared::Game::StationState station;
        Shared::Game::Dice::Scene diceScene;

        /** The last thing the owner's body did that others have to see. See `BodyAction`. */
        Shared::Game::BodyAction bodyAction;

        /** What the owner's body plays while it picks a lock or brews. See `MinigameActions`. */
        Shared::Game::MinigameActions minigameActions;

        /**
         * Whether the owner's body is airborne -- fallen off something or
         * mid-jump -- as its own physics reports it, debounced past the
         * flicker a stair step causes. Snapshot extrapolation uses this to
         * avoid projecting the takeoff velocity through the descent.
         */
        bool inAir = false;

        /** The owner's input, replayed by every receiver. See `PlayerInput`. */
        Shared::Entities::PlayerInput input;

        /**
         * The ladder this player is on, as its level `EntityGuid`, or zero.
         *
         * A held state rather than an event, because climbing is one: a
         * receiver joining mid-climb has to end up on the ladder anyway. The
         * guid rather than an entity id because a guid is the same number on
         * every machine. Receivers put their puppet into the engine's own
         * ladder action from it, which is what makes Mannequin pick the
         * get-on, climb and get-off fragments -- outside that action the same
         * body walks up the rungs in a locomotion fragment.
         */
        std::uint64_t ladderGuid = 0;

        /**
         * Positive up, negative down, zero holding. Handed straight to
         * `SetLadderClimbInput`, which the climb fragment's blend reads. The
         * bound is a wire range, not a claim about attainable speeds.
         */
        static constexpr float kMaxLadderClimbInput = 4.0F;
        static constexpr int kLadderClimbInputBits  = 8;
        float ladderClimbInput                      = 0.0F;

        /** The blade and wheel at a grindstone, sampled with the pose; inactive anywhere else. */
        Shared::Game::SharpeningMotion sharpening;

        /**
         * The window or battlement this player leans out of, as the level
         * `EntityGuid` of its `Battlement` entity, or zero; and which of its
         * slots. Held state like `ladderGuid`: receivers start the engine's
         * own lean on their copy at that slot while it is set and send the
         * copy back out when it clears.
         */
        std::uint64_t battlementGuid = 0;
        std::uint8_t battlementSlot  = 0;

        /**
         * Whether this player has their fists -- or their weapon -- up. A
         * state of its own rather than inferred from the guard: the engine
         * keeps the fists up after the arm comes down.
         */
        bool fistsUp = false;

        /**
         * Monotonic fence for owner-authored combat-state edges.
         *
         * A transient action RPC travels independently of this reliable state
         * channel. Carrying the revision in both lets a receiver wait for the
         * stance that causally preceded the action instead of relying on
         * cross-channel arrival order.
         */
        std::uint32_t combatRevision = 1;

        static std::uint32_t AdvanceCombatRevision(std::uint32_t revision) {
            ++revision;
            return revision != 0 ? revision : 1;
        }

        static bool IsCombatRevisionNewer(std::uint32_t candidate, std::uint32_t reference) {
            return candidate != reference && static_cast<std::uint32_t>(candidate - reference) < 0x80000000U;
        }

        /**
         * The arm guard, in the engine's own levels: 0 arms down, 1 the guard
         * a player holds. Handed straight to `C_CombatActor::RequestGuard`,
         * which builds the same action the owner's game did.
         */
        std::uint8_t guardRequest = 0;

        /**
         * Which direction of the combat star this player is aiming at, as a
         * row of the game's zone table, or -1. KCD2 picks the guard fragment
         * from it. A swing carries its own zone instead, so it is the one that
         * was current when the swing committed.
         */
        std::int16_t combatZone = -1;

        /**
         * The player this one's fighter is locked on to, as a network id, or
         * zero.
         *
         * Read off the owner's own combat target, which its engine's opponent
         * picker chose. Receivers hand it to their puppet's `SetTarget`, the
         * same call, so the puppet squares up against the same person on every
         * machine and the engine makes it the opponent once the body is at
         * rest -- which is what lets a replayed swing land, and a replayed
         * perfect block pull its partner in.
         */
        std::uint64_t combatTargetNetworkId = 0;

        Shared::RPC::MissileState missile;

        /**
         * The player this one carries on their shoulder, as a network id, or
         * zero.
         *
         * A server verdict rather than something the owner publishes: the
         * carrier's own game starts the carry, reports it as `PlayerCarry`,
         * and only a carry the gamemode accepted lands here. So it is a
         * server field -- the owner already knows what it is carrying, and
         * everyone else replays the game's own grab from it on their copies of
         * both bodies. A held state rather than an event, so a player who
         * streams in or joins mid-carry sees the body on the shoulder too.
         */
        std::uint64_t carriedNetworkId = 0;

        /**
         * The item this player carries in their arms, or an invalid class.
         *
         * A server verdict like `carriedNetworkId`: the owner's game starts
         * the carry and reports it as `PlayerCarryItem`, and only one the
         * gamemode accepted lands here. Everyone else replays the game's own
         * carry from it with an item of their own, so a player who streams
         * in mid-carry picks the item up too.
         */
        Shared::RPC::ItemClassId carriedItemClass;

        /**
         * The carryable ground item `carriedItemClass` was taken up from, or
         * zero. Every other client takes its own copy of that prop into this
         * body's arms with the game's pick-up, rather than making an item.
         */
        std::uint64_t carriedGroundItemNetworkId = 0;

        /**
         * The carrier has started putting `carriedItemClass` down. The carry
         * stands until the put-down is reported; every other client plays
         * the put-down from this, alongside the carrier's own.
         */
        bool carriedItemPuttingDown = false;

        /**
         * This player's levels and perks, as the server last checked them.
         *
         * A server verdict like `carriedNetworkId`: the owner publishes a
         * digest to the server alone, the server bounds it by the orders it
         * sent, and only what it accepted lands here. Everyone else answers
         * `C_Soul::HasPerk` for this body's puppet from it. The owner is sent
         * none of it -- its own soul is the live copy.
         */
        Shared::Game::ProgressionView progression;

        /**
         * Promote the body from always-visible to range-streamed the moment it
         * has a pose worth measuring.
         *
         * A fresh avatar sits at the origin until its owner reports where it
         * is, and range culling reads the origin as "nowhere near anyone". So
         * it starts always-visible (see Server::OnPlayerConnect) and leaves
         * that state here, on its own, the first time a real transform lands.
         * Self-healing: a client that never reports simply stays visible
         * rather than becoming a body nobody can see.
         */
        void OnDeserialized(bool transformUpdated) override {
            if (transformUpdated && transformReady) {
                streaming.alwaysVisible = false;
            }
        }

        Framework::Networking::Replication::NametagState *GetNametag() override {
            return &nametag;
        }

        // Delta-coded state channel: owner-authored, reliable, and sent in
        // full on the first update to a fresh peer.
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            // The candidate is read against the last value that came off the
            // wire rather than against the published one, so the delta base
            // stays aligned with what the sender believes it last sent even
            // while a rejected stream is being held back.
            auto candidate = fields.Writing() ? soul : _wireSoul;
            candidate.SerializeFields(fields);
            if (!fields.Writing()) {
                const bool accepted = fields.Good() && candidate.IsSane() && candidate.Follows(_wireSoul);
                if (_soulStreamValid && !accepted) {
                    Framework::Logging::GetLogger("Human")->warn("Invalid soul state for human {}; quarantined until reconstruction", GetNetworkID());
                    WithdrawSoul();
                }
                _soulStreamValid = _soulStreamValid && accepted;
                _wireSoul        = candidate;
                if (_soulStreamValid) {
                    soul = candidate;
                }
            }
            auto readings = statReadings;
            readings.SerializeFields(fields);
            if (!fields.Writing() && fields.Good() && readings.IsSane()) {
                statReadings = readings;
            }
            fields.Field(equipment);
            fields.Field(equipmentWear);
            fields.Field(visorClosed);
            fields.Field(fallSequence);
            fields.Field(fallHitX);
            fields.Field(fallHitY);
            fields.Field(fallHitZ);
            appearance.Serialize(fields);
            disguise.Serialize(fields);
            fields.Field(bodyHidden);
            bodyStance.Serialize(fields);
            bodyAction.Serialize(fields);
            minigameActions.Serialize(fields);
            fields.Field(handItemClass[kRightHand].low);
            fields.Field(handItemClass[kRightHand].high);
            fields.Field(handItemClass[kLeftHand].low);
            fields.Field(handItemClass[kLeftHand].high);
            fields.Field(ladderGuid);
            fields.Field(battlementGuid);
            fields.Field(battlementSlot);
            fields.Field(combatRevision);
            fields.Field(fistsUp);
            fields.Field(guardRequest);
            fields.Field(combatZone);
            fields.Field(combatTargetNetworkId);
            missile.SerializeFields(fields);
            fields.ServerField(carriedNetworkId);
            fields.ServerField(carriedItemClass.low);
            fields.ServerField(carriedItemClass.high);
            fields.ServerField(carriedGroundItemNetworkId);
            fields.ServerField(carriedItemPuttingDown);
            if (fields.ServerAuthored()) {
                station.Serialize(fields);
                diceScene.Serialize(fields);
                animation.Serialize(fields);
                face.Serialize(fields);
                progression.Serialize(fields);
            }

            nametag.Serialize(fields);
        }

        /**
         * The pose, as CryEngine's `SSerializedPlayerInput` carries a player:
         * where the body is, its travel, which way it and its eyes face, and
         * the input that moves it. Every receiver plays the movement from the
         * input; the travel feeds its pose prediction and drift correction.
         */
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            // A receiver that has no soul yet -- a fresh replica, read before
            // the state its construction carries -- has nothing to widen the
            // incarnation against and nothing to call it stale beside. It
            // takes the pose, and construction then delivers the whole value.
            const bool soulKnown    = soul.valid || soul.incarnation != 0;
            const auto previous     = std::make_tuple(poseIncarnation, transformReady, position, velocity, rotation, headLookReady, headLookDirection, inAir, input, ladderClimbInput, sharpening);
            const auto restoreStale = [&]() {
                if (!fields.Writing() && (!fields.Good() || (soulKnown && poseIncarnation != soul.incarnation && !Shared::Game::HumanSoulState::IsNewer(poseIncarnation, soul.incarnation)))) {
                    std::tie(poseIncarnation, transformReady, position, velocity, rotation, headLookReady, headLookDirection, inAir, input, ladderClimbInput, sharpening) = previous;
                }
            };
            constexpr std::uint32_t kIncarnationMask = (1U << kPoseIncarnationBits) - 1U;
            std::uint32_t wireIncarnation            = poseIncarnation & kIncarnationMask;
            fields.BitsRange(wireIncarnation, 0U, kIncarnationMask);
            if (!fields.Writing()) {
                poseIncarnation = WidenIncarnation(wireIncarnation, soul.incarnation);
            }
            fields.Field(transformReady);
            if (!transformReady) {
                restoreStale();
                return;
            }

            fields.FloatBits(position.x, kPoseMinXY, kPoseMaxXY, kPoseBitsXY);
            fields.FloatBits(position.y, kPoseMinXY, kPoseMaxXY, kPoseBitsXY);
            fields.FloatBits(position.z, kPoseMinZ, kPoseMaxZ, kPoseBitsZ);

            // The owner's physics velocity over the ground it stands on: what
            // a receiver predicts the pose forward by, and the travel its
            // puppet is servoed at.
            SignedSteps(fields, velocity.x, kMaxGroundSpeed, kGroundSpeedBits);
            SignedSteps(fields, velocity.y, kMaxGroundSpeed, kGroundSpeedBits);
            SignedSteps(fields, velocity.z, kMaxVerticalSpeed, kVerticalSpeedBits);

            float yaw = fields.Writing() ? YawOf(rotation * glm::vec3(0.0F, 1.0F, 0.0F)) : 0.0F;
            fields.FloatBits(yaw, -kPi, kPi, kYawBits);
            if (!fields.Writing()) {
                rotation = glm::angleAxis(yaw, glm::vec3(0.0F, 0.0F, 1.0F));
            }

            fields.Field(headLookReady);
            if (headLookReady) {
                float lookYaw   = fields.Writing() ? YawOf(headLookDirection) : 0.0F;
                float lookPitch = fields.Writing() ? std::asin(std::clamp(headLookDirection.z, -1.0F, 1.0F)) : 0.0F;
                fields.FloatBits(lookYaw, -kPi, kPi, kLookYawBits);
                fields.FloatBits(lookPitch, -kHalfPi, kHalfPi, kLookPitchBits);
                if (!fields.Writing()) {
                    const float level = std::cos(lookPitch);
                    headLookDirection = glm::vec3(-std::sin(lookYaw) * level, std::cos(lookYaw) * level, std::sin(lookPitch));
                }
            }

            fields.Field(inAir);
            input.Serialize(fields);

            // Beside the pose for the same reason as the tags: it is the
            // blend coordinate of the fragment that pose is in. Zero off a
            // ladder, where the owner holds it, so it costs a bit there.
            bool climbing = ladderClimbInput != 0.0F;
            fields.Field(climbing);
            if (climbing) {
                SignedSteps(fields, ladderClimbInput, kMaxLadderClimbInput, kLadderClimbInputBits);
            }
            else {
                ladderClimbInput = 0.0F;
            }

            // The sharpening loop's blend coordinates and the wheel's speed,
            // for the same reason; a bit everywhere but at a grindstone.
            sharpening.Serialize(fields);
            restoreStale();
        }

      private:
        static constexpr float kPi     = 3.14159265F;
        static constexpr float kHalfPi = 1.57079633F;

        /** Z-axis yaw of a direction: forward (+Y) is zero, left positive. */
        static float YawOf(const glm::vec3 &direction) {
            return std::atan2(-direction.x, direction.y);
        }

        /**
         * The incarnation whose low bits are `low` that lies nearest
         * `reference`, half the wire's span either side of it.
         */
        static std::uint32_t WidenIncarnation(std::uint32_t low, std::uint32_t reference) {
            constexpr std::uint32_t kSpan = 1U << kPoseIncarnationBits;
            const std::uint32_t ahead     = (low - reference) & (kSpan - 1U);
            return ahead < kSpan / 2U ? reference + ahead : reference + ahead - kSpan;
        }

        /**
         * `value` in [-limit, limit] as a whole number of steps either side of
         * an exact zero, over `bits` bits. FloatBits spreads an even count of
         * levels over the range and so has no zero, and a body at rest would
         * read as creeping.
         */
        static void SignedSteps(Framework::Networking::Replication::FieldSerializer &fields, float &value, float limit, int bits) {
            const std::uint32_t half = (1U << (bits - 1)) - 1U;
            const float step         = limit / static_cast<float>(half);
            std::uint32_t steps      = 0;
            if (fields.Writing()) {
                const long rounded = std::lround(std::clamp(value, -limit, limit) / step);
                steps              = static_cast<std::uint32_t>(std::clamp(rounded, -static_cast<long>(half), static_cast<long>(half)) + static_cast<long>(half));
            }
            fields.BitsRange(steps, 0U, 2U * half);
            if (!fields.Writing()) {
                value = (static_cast<float>(steps) - static_cast<float>(half)) * step;
            }
        }



        /**
         * Retires the published body as a revision of its own.
         *
         * A relaying server re-serializes `soul` to everyone else, so simply
         * clearing `valid` in place would leave the incarnation and revision
         * untouched -- and arrive downstream as a state that does not follow
         * the last one, quarantining every observer for a stream that was
         * only ever bad between the owner and the server. Advancing the
         * revision makes the withdrawal a legitimate step in the stream that
         * receivers accept once and act on.
         */
        void WithdrawSoul() {
            if (!soul.valid) {
                return;
            }
            soul.valid    = false;
            soul.revision = Shared::Game::HumanSoulState::Advance(soul.revision);
        }

        // The last state that came off the wire, accepted or not: the delta
        // channel codes against what the sender last sent, which stops being
        // the published state the moment a stream is quarantined.
        Shared::Game::HumanSoulState _wireSoul;

        // Cleared by the first bad soul on this stream and restored only by a
        // fresh construction. A rejected baseline cannot be repaired by the
        // deltas that follow it, because every one of them is coded against
        // the state that was rejected.
        bool _soulStreamValid = true;
    };
} // namespace KCDC::Shared::Entities
