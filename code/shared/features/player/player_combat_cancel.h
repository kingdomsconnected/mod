#pragma once

#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    /** Server command. Consent and inactivity policy belong to the resource. */
    struct CombatCancel {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CombatCancel");
        std::uint64_t first                      = 0;
        std::uint64_t second                     = 0;
        std::uint64_t cancelledAt                = 0;
        std::uint32_t firstIncarnation           = 0;
        std::uint32_t secondIncarnation          = 0;
        std::uint8_t firstEpoch                  = 0;
        std::uint8_t secondEpoch                 = 0;
        bool valid                               = true;

        bool IsSane() const {
            return valid && first != 0 && second != 0 && first != second && cancelledAt != 0;
        }
        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, first) && bs->Serialize(write, second) && bs->Serialize(write, cancelledAt) && bs->Serialize(write, firstIncarnation) && bs->Serialize(write, secondIncarnation) && bs->Serialize(write, firstEpoch) && bs->Serialize(write, secondEpoch);
        }
    };
} // namespace KCDC::Shared::RPC
