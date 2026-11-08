#pragma once

#include "shared/pose_math.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    // A thrown pebble. Every client launches its own native decoy from the
    // thrower's body with these vectors, and the game plays its flight,
    // contact and AI noise there. The server stamps the thrower.
    struct RockLaunched {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::RockLaunched");
        // Decoy.xml gives the native stone 20 m/s; allow configuration variation.
        static constexpr float kMaxSpeed = 40.0F;

        std::uint64_t throwerNetworkId = 0;
        std::uint64_t sequence         = 0;
        std::uint32_t incarnation      = 0;
        std::uint8_t epoch             = 0;
        glm::vec3 position {0.0F};
        glm::vec3 direction {0.0F, 1.0F, 0.0F};
        glm::vec3 velocity {0.0F};
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, throwerNetworkId) && bs->Serialize(write, sequence) && bs->Serialize(write, incarnation) && bs->Serialize(write, epoch) && bs->Serialize(write, position.x) && bs->Serialize(write, position.y) && bs->Serialize(write, position.z)
                    && bs->Serialize(write, direction.x) && bs->Serialize(write, direction.y) && bs->Serialize(write, direction.z) && bs->Serialize(write, velocity.x) && bs->Serialize(write, velocity.y) && bs->Serialize(write, velocity.z);
        }

        bool IsSane() const {
            if (!valid || sequence == 0 || incarnation == 0 || !Game::IsWorldPosition(position) || !Game::IsFinite(direction) || !Game::IsFinite(velocity)) {
                return false;
            }
            const float length = glm::dot(direction, direction);
            const float speed  = glm::dot(velocity, velocity);
            const auto cross   = glm::cross(direction, velocity);
            return length > 0.95F && length < 1.05F && speed > 0.0F && speed <= kMaxSpeed * kMaxSpeed && glm::dot(direction, velocity) > 0.0F
                   && glm::dot(cross, cross) <= speed * 0.001F;
        }
    };
} // namespace KCDC::Shared::RPC
