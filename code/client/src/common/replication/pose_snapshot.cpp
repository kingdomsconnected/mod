#include "pose_snapshot.h"

namespace KCDC::Common::Replication {
    PoseSnapshot PoseSnapshotPolicy::Blend(const PoseSnapshot &a, const PoseSnapshot &b, float alpha, float) {
        // Gait describes travel over this bracket, so it comes from the newer
        // pose, as the velocity beside it does. Stance is a discrete pose
        // change, so retain the older stance until the newer timestamp.
        PoseSnapshot out = b;
        out.stanceTag    = alpha < 1.0F ? a.stanceTag : b.stanceTag;
        out.inAir        = alpha < 1.0F ? a.inAir : b.inAir;
        out.input        = alpha < 1.0F ? a.input : b.input;
        out.fallSequence = alpha < 1.0F ? a.fallSequence : b.fallSequence;
        out.position     = glm::mix(a.position, b.position, alpha);
        out.rotation     = glm::slerp(a.rotation, b.rotation, alpha);
        out.lookPitch    = a.lookPitch + (b.lookPitch - a.lookPitch) * alpha;

        // Interpolate the reported velocity. Differentiating sparse positions
        // would turn packet timing and corrections into movement spikes.
        out.velocity = glm::mix(a.velocity, b.velocity, alpha);
        return out;
    }

    PoseSnapshot PoseSnapshotPolicy::Extrapolate(const PoseSnapshot &newest, float dtSeconds) {
        PoseSnapshot out = newest;

        // Only measured velocity describes the vertical arc. An animation
        // request can contain an impulse or zero during flight; extrapolating
        // its vertical component would invent travel during the descent.
        glm::vec3 travel = newest.velocity;
        if (newest.inAir && !newest.hasPhysicsVelocity) {
            travel.z = 0.0F;
        }
        out.position += travel * dtSeconds;

        // Hold the last heading past the newest packet. Interpolation
        // reaches the next packet's yaw once it arrives.
        return out;
    }

    void HoldLocomotion(PoseSnapshot &pose) {
        pose.velocity     = glm::vec3(0.0F);
        pose.input.Release();
        pose.moveSpeedTag = Shared::Game::kNoAnimationTag;
        pose.moveDirTag   = Shared::Game::kNoAnimationTag;
        pose.pace         = 0;
    }

    const Framework::Utils::SnapshotBufferConfig &PoseBufferConfig() {
        // The warp threshold: a body that moved further than this between two
        // packets was put there rather than having walked, so the buffer
        // latches the new pose instead of sweeping the body across the gap.
        //
        // Limit extrapolation to 50 ms so a stop does not create a large
        // position error before the next packet arrives.
        static const Framework::Utils::SnapshotBufferConfig config {
            .maxExtrapolationMs = 50.0F,
            .warpBase           = 8.0F,
            .warpPerSpeed       = 4.0F,
        };
        return config;
    }
} // namespace KCDC::Common::Replication
