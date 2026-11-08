#pragma once

#include "shared/features/player/player_input.h"
#include "shared/game/animation_tags.h"

#include <utils/snapshot_buffer.h>

#include <mafianet/get_time.h>

#include <glm/ext.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace KCDC::Common::Replication {
    /**
     * One received pose, held until the moment it describes comes around.
     *
     * The locomotion tags travel with it rather than being read off the entity
     * at draw time: a buffered body is drawn behind the newest packet, so tags
     * read live would start a pace before the buffered body moves. The current
     * wire channels arrive independently: these are the latest tags available
     * when the transform arrived, not a guaranteed sender-coherent sample.
     */
    struct PoseSnapshot {
        glm::vec3 position = glm::vec3(0.0F);

        // The owner's reported velocity, interpolated with the position.
        // Humans send measured travel for native movement reconciliation;
        // other publishers send the request that drives their animation.
        glm::vec3 velocity = glm::vec3(0.0F);

        // Humans publish measured physics velocity, including the current
        // vertical arc. Other publishers still send animation-request velocity.
        bool hasPhysicsVelocity = false;

        // The owner's body facing, yaw-only off the wire: the base a native
        // player body is turned to, and the frame the owner's move axes
        // below are relative to.
        glm::quat rotation = glm::identity<glm::quat>();

        std::uint8_t moveSpeedTag = Shared::Game::kNoAnimationTag;
        std::uint8_t moveDirTag   = Shared::Game::kNoAnimationTag;
        std::uint8_t stanceTag    = Shared::Game::kNoAnimationTag;

        // The owner's pace as an `E_MovementSpeed` ordinal, for a body that
        // resolves its own locomotion tags from it rather than taking the
        // owner's. Zero is standing.
        std::uint8_t pace = 0;

        // Whether the owner was airborne at this pose; discrete like the
        // tags, so it holds until the timestamp that changes it.
        bool inAir = false;

        // The owner's input, replayed through a native player's own input
        // handlers; AI bodies use the tags above. Discrete like inAir: it
        // holds until the timestamp that changes it.
        Shared::Entities::PlayerInput input;

        // The falls the owner's body had taken (`HumanEntity::fallSequence`).
        // Discrete like the input, so a native player body takes the next
        // one when it is drawn where its owner fell, not when the packet
        // that counted it arrives.
        std::uint8_t fallSequence = 0;

        // How far up or down the owner looks, radians, sampled on the same
        // clock as the input above: the pitch a native player body's view
        // carries, which its aim is derived from. Its yaw is the body's.
        float lookPitch = 0.0F;
    };

    /** How two received poses combine: the continuous fields blend, the tags do not. */
    struct PoseSnapshotPolicy {
        static glm::vec3 Position(const PoseSnapshot &snapshot) {
            return snapshot.position;
        }

        static glm::vec3 Velocity(const PoseSnapshot &snapshot) {
            return snapshot.velocity;
        }

        static PoseSnapshot Blend(const PoseSnapshot &a, const PoseSnapshot &b, float alpha, float spanMs);
        static PoseSnapshot Extrapolate(const PoseSnapshot &newest, float dtSeconds);
    };

    /** Stop locomotion on a held presentation pose, preserving its stance. */
    void HoldLocomotion(PoseSnapshot &pose);

    /**
     * Tuning shared by players and horses alike: a rider and the animal under
     * it are the same pose a frame apart, and two clocks would let the body
     * drift out of its own saddle.
     */
    const Framework::Utils::SnapshotBufferConfig &PoseBufferConfig();

    /**
     * When `buffer` should be sampled to draw this frame, whatever kind of
     * pose it holds. Call once a frame per buffer.
     *
     * The buffer's own presentation clock, which runs at wall speed and slews
     * toward its adaptive delay: subtracting the delay from now instead makes
     * every change of that delay a jump of the drawn body, backwards on a
     * late packet. It is also the clock events on that body are scheduled
     * against, so a swing plays where the body is drawn swinging.
     */
    template <typename TSnapshot, typename TPolicy>
    MafiaNet::Time PoseRenderTime(Framework::Utils::SnapshotBuffer<TSnapshot, TPolicy> &buffer) {
        return buffer.RenderTime(MafiaNet::GetTime());
    }
} // namespace KCDC::Common::Replication
