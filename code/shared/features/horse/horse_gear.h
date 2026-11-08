#pragma once

#include "shared/game/horse_gear_catalog.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>
#include <string>

namespace KCDC::Shared::RPC {
    /**
     * A client telling the server what its copy of a horse is now wearing.
     *
     * Sent by the rider's client when they dressed the horse at the game's
     * horse inventory screen, and by nobody else: any other copy that differs
     * from the record does so because its engine undressed it, and puts the
     * gear back instead. The server checks the report against the gear
     * catalog, lets scripts refuse it, and answers with a new `gearRevision`
     * either way, so a refused change is put back on the copy that made it.
     */
    struct HorseGearReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::HorseGearReport");

        std::uint64_t horseNetworkId = 0;

        /** What the copy wears, packed as `Shared::Game::PackHorseGear` writes it. */
        std::string gear;

        /** The `gearRevision` the reporting copy had applied when it looked. */
        std::uint32_t basedOnRevision = 0;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, horseNetworkId) && bs->Serialize(write, gear) && bs->Serialize(write, basedOnRevision);
        }

        bool IsSane() const {
            return valid && horseNetworkId != 0 && gear.size() <= Game::kMaxPackedHorseGear && gear.size() % Game::kPackedHorseGearWidth == 0;
        }
    };
} // namespace KCDC::Shared::RPC
