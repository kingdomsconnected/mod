#pragma once

#include "shared/game/animation_tags.h"

#include <networking/replication/network_entity.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * What an NPC is, beyond a body standing somewhere.
     *
     * Server-authored, so every one of these is a decision rather than a
     * measurement, and the simulator adopts them through the forced-state
     * channel like everything else the server decides.
     */
    enum class NpcFlags : std::uint16_t {
        None = 0,
        /** Damage is refused before it reaches the ledger. */
        Invulnerable = 1U << 0U,
        /** The body holds its pose whatever its intent says; a statue. */
        Frozen = 1U << 1U,
        /** Clients watch the use key against this body and report presses. */
        Interactable = 1U << 2U,
        /** Draw this NPC's name over it, the way a player's is drawn. */
        ShowNametag = 1U << 3U,
        /** Its corpse keeps its inventory for whoever searches it. */
        Lootable = 1U << 4U,
        /**
         * Let the simulator steer the body through the engine's own movement
         * controller instead of advancing the pose itself. See
         * `NpcEntity::locomotion`.
         */
        NativeSteering = 1U << 5U,
    };

    inline NpcFlags operator|(NpcFlags lhs, NpcFlags rhs) {
        return static_cast<NpcFlags>(static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs));
    }

    inline NpcFlags &operator|=(NpcFlags &lhs, NpcFlags rhs) {
        lhs = lhs | rhs;
        return lhs;
    }

    inline bool HasFlag(NpcFlags value, NpcFlags flag) {
        return (static_cast<std::uint16_t>(value) & static_cast<std::uint16_t>(flag)) != 0;
    }

    /** Every flag this build knows, for rejecting bits from a newer peer. */
    inline constexpr std::uint16_t kNpcFlagMask = 0x003FU;

    /**
     * What the server wants this NPC to be doing.
     *
     * Held state with a revision rather than a queue of orders. A new owner
     * or a client joining later reads the current intent and applies it
     * without needing earlier messages.
     */
    enum class NpcIntentKind : std::uint8_t {
        /** Stand where you are, facing where you face. */
        Hold = 0,
        /** Walk to `point` and report `Reached` inside `radius`. */
        MoveTo = 1,
        /** Keep `targetNetworkId` within `radius`, following it as it moves. */
        Follow = 2,
        /** Move away from `point` until `radius` metres separate you from it. */
        Flee = 3,
        /** Hold, but look at `targetNetworkId` if set, else at `point`. */
        LookAt = 4,
        /** Play `animationId` where you stand, once or on a loop. */
        PlayAnim = 5,
        /** Face `targetNetworkId` and be available for conversation. */
        Talk = 6,
        /** Pursue and fight the designated player or NPC until replaced. */
        Attack = 7,
        Count,
    };

    /** The pace a move runs at; a Mannequin vocabulary rather than a speed. */
    enum class NpcSpeed : std::uint8_t {
        Walk = 0,
        Jog  = 1,
        Run  = 2,
        Count,
    };

    inline NpcSpeed NpcSpeedFromName(std::string_view name, NpcSpeed fallback = NpcSpeed::Count) {
        if (name == "walk") {
            return NpcSpeed::Walk;
        }
        if (name == "jog") {
            return NpcSpeed::Jog;
        }
        if (name == "run") {
            return NpcSpeed::Run;
        }
        return fallback;
    }

    /**
     * The engine's `E_MovementSpeed` ordinal for a pace.
     *
     * Not a speed. `C_Actor::UpdateMannequinTags` resolves the MoveSpeed
     * animation tag from `CMovementRequest::pseudoSpeed`, which is this
     * five-state ordinal -- idle 0, walk 1, run 2, sprint 3, dash 4 -- and
     * `ResolveMoveSpeedTag` picks the tier as `round(pseudoSpeed) - 1`. A body
     * whose pseudo-speed is below 0.1 leaves the MoveSpeed group empty, which is
     * correct for standing still and is why a stationary body has no pace tag.
     *
     * Feeding metres per second here instead is the difference between a body
     * that walks and a body that slides: 1.35 and 3.1 are not ordinals, and the
     * tier they round to is not the pace that was asked for.
     */
    inline float NpcSpeedPseudoOrdinal(NpcSpeed speed) {
        switch (speed) {
        case NpcSpeed::Jog: return 2.0F; // run
        case NpcSpeed::Run: return 3.0F; // sprint
        case NpcSpeed::Walk:
        default: return 1.0F;
        }
    }

    /** Metres a second each pace covers, for the simulator that has to move a body at one. */
    inline float NpcSpeedMetresPerSecond(NpcSpeed speed) {
        switch (speed) {
        case NpcSpeed::Jog: return 3.1F;
        case NpcSpeed::Run: return 5.1F;
        case NpcSpeed::Walk:
        default: return 1.35F;
        }
    }

    /** How the simulator is getting the body to where the intent wants it. */
    enum class NpcLocomotion : std::uint8_t {
        /**
         * The simulator advances the pose itself and publishes it, and every
         * client -- including the simulator -- animates that pose through the
         * same locomotion override a remote player's body runs on. Predictable
         * and unaware of what is in the way.
         *
         * **It does not animate, and cannot as written.** The pace and
         * direction tags a body is drawn with are raw Mannequin ids, and the
         * simulator fills them by reading them back off its own body -- which
         * in this mode nothing is animating, so it reads "no tag", publishes
         * "no tag", and clears the tags that would have made it walk. A remote
         * player escapes the circle because its tags originate on a real body
         * somewhere and are replicated; an NPC has no such body. Giving this
         * mode a walk cycle means choosing tag ids rather than reading them,
         * which means resolving them by name through `CTagDefinition::FindCRC`.
         * Until then a kinematic body slides, and this is not the default.
         */
        Kinematic = 0,

        /**
         * The simulator hands the engine's own movement controller a move
         * target and lets the actor walk there, which is what gives a body
         * real footfalls and real turns. The engine decides the pose; the
         * simulator only reports it -- and the tags it reports are real ones,
         * so observers animate too.
         *
         * This is the default even with the body's AI disabled.
         * `C_ActorMovementController::RequestMovement` merges the request
         * and always returns true, so its result does not prove movement.
         * A body that does not move under native steering falls back to the
         * kinematic stepper.
         */
        Native = 1,
        Count,
    };

    /** What the simulator says has become of the intent it was given. */
    enum class NpcIntentStatus : std::uint8_t {
        /** Nothing to report: no intent, or nobody is simulating this NPC. */
        Idle = 0,
        Running = 1,
        /** A move arrived, an animation finished, a look settled. */
        Reached = 2,
        /** Progress stopped without arriving -- something is in the way. */
        Blocked = 3,
        /** The intent cannot be carried out at all: no target, no body. */
        Failed = 4,
        Count,
    };

    inline const char *NpcIntentStatusName(NpcIntentStatus status) {
        switch (status) {
        case NpcIntentStatus::Running: return "running";
        case NpcIntentStatus::Reached: return "reached";
        case NpcIntentStatus::Blocked: return "blocked";
        case NpcIntentStatus::Failed: return "failed";
        default: return "idle";
        }
    }

    /**
     * One intent, whole. Every kind reads the same fields, which keeps the wire
     * shape fixed and the simulator's switch honest: a field a kind does not
     * use is simply not read by it.
     */
    struct NpcIntent {
        NpcIntentKind kind = NpcIntentKind::Hold;
        NpcSpeed speed     = NpcSpeed::Walk;

        /** Mannequin stance group to hold while running this, or `kNoAnimationTag`. */
        std::uint8_t stanceTag = Shared::Game::kNoAnimationTag;

        /** Where: a move destination, or what a flee runs away from. */
        glm::vec3 point = glm::vec3(0.0F);

        /** Who: the NetworkID of the entity this intent is about, or 0. */
        std::uint64_t targetNetworkId = 0;

        /** Metres: arrival radius, follow distance, flee distance. */
        float radius = 1.5F;

        /**
         * Metres of route that go on past `point` when it is a corner of a
         * server route, and 0 when `point` is where the move ends.
         *
         * The engine reads a move target as the end of a path unless told
         * otherwise -- `CMovementRequest::m_distanceToPathEnd`, which its
         * corner smoother and movement transitions brake and plan a stop
         * transition by -- so a body given corners one at a time would stop at
         * every one of them. The simulator hands this on, and keeps walking
         * through a corner it reaches before the next one arrives.
         */
        float pathBeyond = 0.0F;

        /** Which emote/animation a `PlayAnim` plays. */
        std::uint16_t animationId = 0;

        /** Whether a `PlayAnim` repeats until the intent changes. */
        bool loop = false;

        /** Whether the body turns to face what it is walking to or looking at. */
        bool faceTarget = true;

        /** Use the equipped bow instead of melee automation for an attack. */
        bool bow = false;

        /** How close an attack closes before it fights: arm's length, or a bow's standoff. */
        static constexpr float kMeleeAttackRadius = 2.0F;
        static constexpr float kBowAttackRadius   = 18.0F;

        /** How far an engaged fight may drift apart before the body closes in again. */
        static constexpr float kMeleeEngagedRange = 4.0F;
        static constexpr float kBowEngagedRange   = 22.0F;

        /** The farthest target an attack order may name. */
        static constexpr float kMaxAttackRange = 128.0F;

        /** How long a simulated bow holds its draw before it looses. */
        static constexpr std::uint32_t kBowDrawHoldMs = 2000;

        /** Bounds on what a peer can be asked to run, checked on receipt. */
        static constexpr float kMaxRadius = 512.0F;
        static constexpr float kWorldLimit = 32768.0F;

        /** The longest route a corner can say lies beyond it. */
        static constexpr float kMaxPathBeyond = 65536.0F;

        /**
         * How far above or below a target a body may stand and still have
         * arrived. Arrival is measured across the ground so that a target on a
         * slope or a step is still arrived at, but a body standing under a
         * balcony has not reached a point on it.
         */
        static constexpr float kArriveHeight = 2.0F;

        /** The least arrival radius a target is given. */
        static constexpr float kMinArriveRadius = 0.25F;

        bool IsSane() const {
            const bool finitePoint = std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
            const bool boundedPoint = std::fabs(point.x) <= kWorldLimit && std::fabs(point.y) <= kWorldLimit && std::fabs(point.z) <= kWorldLimit;
            return (kind != NpcIntentKind::Attack || targetNetworkId != 0) && kind < NpcIntentKind::Count && speed < NpcSpeed::Count && finitePoint && boundedPoint && std::isfinite(radius) && radius >= 0.0F && radius <= kMaxRadius && std::isfinite(pathBeyond)
                   && pathBeyond >= 0.0F && pathBeyond <= kMaxPathBeyond;
        }

        /** Whether this intent needs the body to go somewhere. */
        bool IsMotion() const {
            return kind == NpcIntentKind::MoveTo || kind == NpcIntentKind::Follow || kind == NpcIntentKind::Attack || kind == NpcIntentKind::Flee;
        }

        /**
         * A point just past the flee distance, straight away from the threat.
         * At the body's own height rather than the threat's: on a slope the
         * threat's height can be metres off the ground the body runs over.
         */
        glm::vec3 FleeDestination(const glm::vec3 &position) const {
            const glm::vec2 away      = glm::vec2(position) - glm::vec2(point);
            const float length        = glm::length(away);
            const glm::vec2 direction = length > 0.001F ? away / length : glm::vec2(1.0F, 0.0F);
            return glm::vec3(glm::vec2(point) + direction * (radius + 1.0F), position.z);
        }

        bool HasArrived(const glm::vec3 &position, const glm::vec3 &target) const {
            // Flee measures separation from the threat, not its destination.
            return kind == NpcIntentKind::Flee ? glm::distance(glm::vec2(position), glm::vec2(point)) >= radius : glm::distance(glm::vec2(position), glm::vec2(target)) <= std::max(radius, kMinArriveRadius) && std::fabs(position.z - target.z) <= kArriveHeight;
        }

        bool operator==(const NpcIntent &other) const = default;

        // One body for both channels, as CharacterAppearance does: the field
        // channel and the forced-state channel must not drift into two formats,
        // and for an NPC they carry the same struct by construction.
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            auto wireKind  = static_cast<std::uint8_t>(kind);
            auto wireSpeed = static_cast<std::uint8_t>(speed);
            fields.Field(wireKind);
            fields.Field(wireSpeed);
            fields.Field(stanceTag);
            fields.Field(point.x);
            fields.Field(point.y);
            fields.Field(point.z);
            fields.Field(targetNetworkId);
            fields.Field(radius);
            fields.Field(pathBeyond);
            fields.Field(animationId);
            fields.Field(loop);
            fields.Field(faceTarget);
            fields.Field(bow);
            if (!fields.Writing()) {
                kind  = wireKind < static_cast<std::uint8_t>(NpcIntentKind::Count) ? static_cast<NpcIntentKind>(wireKind) : NpcIntentKind::Hold;
                speed = wireSpeed < static_cast<std::uint8_t>(NpcSpeed::Count) ? static_cast<NpcSpeed>(wireSpeed) : NpcSpeed::Walk;
            }
        }
    };
} // namespace KCDC::Shared::Game
