#pragma once

#include "shared/pose_math.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>
#include <networking/replication/network_entity.h>

namespace KCDC::Shared::RPC {
    /**
     * Server -> one client: film this player's view from a fixed point, or
     * give them their own view back.
     *
     * A shot is a camera at `position` looking at `lookAt`, reached by a cut
     * or by a glide of `durationMs`. `active` false is the way back to the
     * player's own eyes, over `durationMs` again; the other fields are then
     * unused. It is the last word, not a queue: a shot arriving while one
     * holds or glides starts from wherever the view is now.
     *
     * Nothing about it is state the server keeps. A client that joins later,
     * or reloads its level, simply has no shot until it is sent another.
     */
    struct CameraShot {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CameraShot");

        /** A minute: longer than that is a camera path, not a glide. */
        static constexpr std::uint32_t kMaxDurationMs = 60000;

        /** The lens a shot may ask for, in vertical degrees; 0 keeps the player's own. */
        static constexpr float kMinFieldOfView = 20.0F;
        static constexpr float kMaxFieldOfView = 120.0F;

        bool active = true;
        glm::vec3 position {0.0F};
        glm::vec3 lookAt {0.0F};

        /** 0 is a cut. */
        std::uint32_t durationMs = 0;

        float fieldOfView = 0.0F;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(active);
            fields.Field(position.x);
            fields.Field(position.y);
            fields.Field(position.z);
            fields.Field(lookAt.x);
            fields.Field(lookAt.y);
            fields.Field(lookAt.z);
            fields.Field(durationMs);
            fields.Field(fieldOfView);
            _complete = fields.Good();
        }

        bool IsSane() const {
            if (!_complete || durationMs > kMaxDurationMs) {
                return false;
            }
            if (!active) {
                return true;
            }
            if (!Shared::Game::IsFinite(position) || !Shared::Game::IsFinite(lookAt) || glm::dot(lookAt - position, lookAt - position) < 1e-4F) {
                return false;
            }
            return fieldOfView == 0.0F || (fieldOfView >= kMinFieldOfView && fieldOfView <= kMaxFieldOfView);
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
