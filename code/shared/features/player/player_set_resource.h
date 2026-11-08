#pragma once

#include "shared/features/player/player_soul_state.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    /** Server -> owner: set a writable stat on the identified life. */
    struct SetPlayerResource {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SetPlayerResource");

        std::uint64_t networkId   = 0;
        std::uint32_t incarnation = 0;
        std::uint8_t epoch        = 0;
        std::uint8_t resource     = Game::HumanSoulState::kHealth;
        float value               = 0.0F;
        bool valid                = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, networkId) && bs->Serialize(write, incarnation) && bs->Serialize(write, epoch) && bs->Serialize(write, resource) && bs->Serialize(write, value);
        }

        static bool IsValueSane(std::uint8_t resource, double value) {
            if (resource >= Game::HumanSoulState::kResourceCount || !std::isfinite(value) || value < 0.0 || value > Game::HumanSoulState::kMaxNativeValue) {
                return false;
            }
            const float nativeValue = static_cast<float>(value);
            // Never narrow a positive JS number into the zero-health kill request.
            // C_Soul::SetState (0x1809DC648) ignores nonzero health <= 1e-5.
            return value == 0.0 || (nativeValue > 0.0F && (resource != Game::HumanSoulState::kHealth || nativeValue > 1e-5F));
        }

        bool IsSane() const {
            return valid && networkId != 0 && incarnation != 0 && IsValueSane(resource, value);
        }

        bool AppliesTo(std::uint64_t id, std::uint8_t ownerEpoch, const Game::HumanSoulState &soul) const {
            return IsSane() && networkId == id && epoch == ownerEpoch && soul.valid && incarnation == soul.incarnation && soul.resources[Game::HumanSoulState::kHealth].current > 0.0F;
        }
    };
} // namespace KCDC::Shared::RPC
