#pragma once

#include "shared/features/player/player_combat_hit_report.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * Server -> the victim's owner: an attacker's claim of a blow the
     * victim's own machine never reported. Its copy of the swing missed the
     * real body -- the attacker stepped in, and the copy's step arrived late
     * -- so the claim was resolved against the copy's guard, which lags the
     * real one by a round trip. The victim's machine says how it was really
     * defending against that swing, and the ruling goes by that.
     */
    struct CombatClaimQuery {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CombatClaimQuery");

        /** The server's name for the held claim; the answer repeats it. */
        std::uint64_t queryId = 0;

        std::uint64_t attackerNetworkId = 0;
        std::uint32_t attackGeneration  = 0;

        bool valid = true;

        bool IsSane() const {
            return valid && queryId != 0 && attackerNetworkId != 0 && attackGeneration != 0;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, queryId) && bs->Serialize(write, attackerNetworkId) && bs->Serialize(write, attackGeneration);
        }
    };

    /** Victim -> server: how the victim was defending against a claimed swing. */
    struct CombatClaimAnswer {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CombatClaimAnswer");

        std::uint64_t queryId = 0;

        /** Whether this machine played that swing at all; without it the claim stands as made. */
        bool known = false;

        /** `CombatHitFlags`: blocked, or blocked and perfect; zero for a guard that was down. */
        std::uint8_t flags = 0;

        bool valid = true;

        bool IsSane() const {
            const std::uint8_t allowed = CombatHitFlags::kBlocked | CombatHitFlags::kPerfectBlock;
            return valid && queryId != 0 && (flags & ~allowed) == 0 && ((flags & CombatHitFlags::kPerfectBlock) == 0 || (flags & CombatHitFlags::kBlocked) != 0) && (known || flags == 0);
        }

        /** A known open guard replaces a stale block just as a block replaces a hit. */
        void Amend(CombatHitReport &report) const {
            if (!known) {
                return;
            }
            if (report.flags != flags) {
                report.recoilRowIndex = CombatHitReport::kNoRow;
                report.hasImpact      = false;
            }
            report.flags = flags;
            if (flags != 0) {
                report.hitRowIndex    = CombatHitReport::kNoRow;
                report.hitRecordFlags = 0;
            }
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, queryId) && bs->Serialize(write, known) && bs->Serialize(write, flags);
        }
    };
} // namespace KCDC::Shared::RPC
