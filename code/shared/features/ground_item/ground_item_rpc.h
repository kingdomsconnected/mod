#pragma once

#include "shared/item_stack.h"
#include "shared/pose_math.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>

namespace KCDC::Shared::RPC {
    namespace Detail {
        inline bool IsFinitePose(const glm::vec3 &position, const glm::quat &rotation) {
            return Shared::Game::IsFinite(position) && Shared::Game::IsFinite(rotation);
        }
    } // namespace Detail

    struct GroundItemPickupStarted {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::GroundItemPickupStarted");

        std::uint64_t groundItemNetworkId = 0;
        std::uint64_t pickupToken         = 0;
        bool valid                        = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, groundItemNetworkId) && bs->Serialize(write, pickupToken);
        }

        bool IsSane() const {
            return valid && groundItemNetworkId != 0 && pickupToken != 0;
        }
    };

    struct GroundItemPickupCompleted {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::GroundItemPickupCompleted");

        std::uint64_t groundItemNetworkId = 0;
        std::uint64_t pickupToken         = 0;
        bool succeeded                    = false;
        bool valid                        = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, groundItemNetworkId) && bs->Serialize(write, pickupToken) && bs->Serialize(write, succeeded);
        }

        bool IsSane() const {
            return valid && groundItemNetworkId != 0 && pickupToken != 0;
        }
    };

    enum class GroundItemPickupDisposition : std::uint8_t {
        Reserved,
        Won,
        Lost,
        Released,
    };

    struct GroundItemPickupResult {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::GroundItemPickupResult");

        std::uint64_t groundItemNetworkId       = 0;
        std::uint64_t pickupToken               = 0;
        GroundItemPickupDisposition disposition = GroundItemPickupDisposition::Lost;
        bool valid                              = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t wireDisposition = static_cast<std::uint8_t>(disposition);
            valid                        = bs->Serialize(write, groundItemNetworkId) && bs->Serialize(write, pickupToken) && bs->Serialize(write, wireDisposition);
            if (!write) {
                if (wireDisposition > static_cast<std::uint8_t>(GroundItemPickupDisposition::Released)) {
                    valid = false;
                    return;
                }
                disposition = static_cast<GroundItemPickupDisposition>(wireDisposition);
            }
        }

        bool IsSane() const {
            return valid && groundItemNetworkId != 0 && pickupToken != 0;
        }
    };
} // namespace KCDC::Shared::RPC
