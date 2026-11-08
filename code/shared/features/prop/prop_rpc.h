#pragma once

#include "shared/pose_math.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    /** What a script asked a rigid prop's body to do. */
    enum class PropMotionKind : std::uint8_t {
        /** A push: `linear` is the impulse in kg m/s, at `point` or the centre of mass; `angular` a twist on top. */
        Impulse,
        /** `linear` and `angular` become its velocity outright, in m/s and rad/s. */
        Velocity,
        /** Starts a resting body simulating where it lies. */
        Wake,
        /** Stops it where it is, until something touches it. */
        Sleep,
    };

    inline bool IsPropMotionKind(std::uint8_t value) {
        return value <= static_cast<std::uint8_t>(PropMotionKind::Sleep);
    }

    /**
     * Server -> the client simulating a rigid prop: do this to its body, once.
     *
     * Only the simulator runs the body, and everyone else follows the pose it
     * streams, so a motion goes to that one client. It can arrive before the
     * client learns it simulates the prop -- a motion that elects a simulator
     * is sent the moment the election makes it the owner -- or before the
     * replica it names, so a receiver holds it for a moment rather than
     * dropping it.
     */
    struct PropMotion {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PropMotion");

        /** Past these a push is a launch, and a velocity outruns the physics step. */
        static constexpr float kMaxImpulse         = 1.0e7F;
        static constexpr float kMaxVelocity        = 200.0F;
        static constexpr float kMaxAngularVelocity = 100.0F;

        std::uint64_t propNetworkId = 0;
        std::uint8_t kind           = static_cast<std::uint8_t>(PropMotionKind::Wake);
        glm::vec3 linear {0.0F};
        glm::vec3 angular {0.0F};
        /** Where an impulse lands, in world space; meaningful only with `hasPoint`. */
        glm::vec3 point {0.0F};
        bool hasPoint = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(propNetworkId);
            fields.Field(kind);
            fields.Field(linear.x);
            fields.Field(linear.y);
            fields.Field(linear.z);
            fields.Field(angular.x);
            fields.Field(angular.y);
            fields.Field(angular.z);
            fields.Field(hasPoint);
            fields.Field(point.x);
            fields.Field(point.y);
            fields.Field(point.z);
            _complete = fields.Good();
        }

        PropMotionKind Kind() const {
            return static_cast<PropMotionKind>(kind);
        }

        bool IsSane() const {
            if (!_complete || propNetworkId == 0 || !IsPropMotionKind(kind) || !Shared::Game::IsFinite(linear) || !Shared::Game::IsFinite(angular)) {
                return false;
            }
            if (hasPoint && !Shared::Game::IsWorldPosition(point)) {
                return false;
            }
            switch (Kind()) {
            case PropMotionKind::Impulse: return glm::dot(linear, linear) <= kMaxImpulse * kMaxImpulse && glm::dot(angular, angular) <= kMaxImpulse * kMaxImpulse;
            case PropMotionKind::Velocity: return glm::dot(linear, linear) <= kMaxVelocity * kMaxVelocity && glm::dot(angular, angular) <= kMaxAngularVelocity * kMaxAngularVelocity;
            case PropMotionKind::Wake:
            case PropMotionKind::Sleep: return true;
            }
            return false;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
