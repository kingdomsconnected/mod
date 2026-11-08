#pragma once

#include "shared/features/player/player_combat_hit_report.h"

#include <mafianet/bit_stream.h>
#include <mafianet/time.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * Server -> peers: the referee's ruling on one blow, in the order it
     * ruled.
     *
     * Sent to the victim's owner, which takes the health off its body; to the
     * attacker's owner; and to every peer that holds either body. Each plays
     * what its engine has not already played: the machine that resolved the
     * blow (`resolver`) ran the consequences on its own bodies at contact.
     * After a victim's report the attacker's machine runs the recoil on its
     * real body; after an attacker's claim the victim's machine runs the hit
     * reaction on its real body and spends the stamina; everyone else plays
     * the recoil, the reaction a claim carries and the impact on their copies.
     */
    struct CombatOutcome {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CombatOutcome");

        /** The referee's own count, one per ruling; strictly increasing on every peer's ordered channel. */
        std::uint64_t sequence = 0;

        std::uint8_t source = static_cast<std::uint8_t>(CombatHitSource::Melee);

        /** A `CombatHitResolver`: whose machine already played the blow. */
        std::uint8_t resolver = static_cast<std::uint8_t>(CombatHitResolver::Victim);

        std::uint64_t attackerNetworkId = 0;
        std::uint64_t victimNetworkId   = 0;

        /** The victim's life the ruling is about; a body that has since died or respawned ignores it. */
        std::uint32_t victimIncarnation = 0;
        std::uint8_t victimEpoch        = 0;

        std::uint32_t attackGeneration = 0;
        std::uint64_t shotSequence     = 0;

        /** Health the victim loses, after the gamemode had its say; zero when it refused. */
        float healthLoss = 0.0F;

        /** Stamina the blow cost the victim. Spent by the victim's machine only after a claim; a report's was spent at contact. */
        float staminaLoss = 0.0F;

        /**
         * Whether this blow may take the last of the victim's health. False
         * for a blow the engine itself would have stopped short of a kill --
         * a punch, a kick -- which leaves the victim on the edge instead.
         */
        bool mayKill = false;

        /** A `Game::DamageReason`, carried for the death the blow may cause. */
        std::uint8_t reason = 0;

        std::uint8_t flags = 0;

        std::uint32_t recoilRowIndex = CombatHitReport::kNoRow;

        /** After a claim: the hit reaction the victim's body plays, and its record bits; `kNoRow` otherwise. */
        std::uint32_t hitRowIndex   = CombatHitReport::kNoRow;
        std::uint8_t hitRecordFlags = 0;

        bool hasImpact = false;
        CombatImpact impact;

        /** When the server ruled, on its clock; a receiver shifts it onto its own. */
        MafiaNet::Time ruledAt = 0;

        /**
         * When the blow's first account -- the victim's report or the
         * attacker's claim -- reached the server, on its clock: as close to
         * the contact as the server can say. How late the impact would sound
         * is measured from here.
         */
        MafiaNet::Time reportedAt = 0;

        bool valid = true;

        CombatHitResolver Resolver() const {
            return static_cast<CombatHitResolver>(resolver);
        }

        bool IsSane() const {
            return valid && sequence != 0 && source <= static_cast<std::uint8_t>(CombatHitSource::Missile) && resolver <= static_cast<std::uint8_t>(CombatHitResolver::Attacker) && attackerNetworkId != 0 && victimNetworkId != 0 && attackerNetworkId != victimNetworkId
                   && victimIncarnation != 0 && (flags & ~CombatHitFlags::kAll) == 0 && hitRecordFlags <= 3 && std::isfinite(healthLoss) && healthLoss >= 0.0F && healthLoss <= CombatHitReport::kMaxHealthLoss && std::isfinite(staminaLoss) && staminaLoss >= 0.0F
                   && staminaLoss <= CombatHitReport::kMaxStaminaLoss && (!hasImpact || impact.IsSane());
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, sequence) && bs->Serialize(write, source) && bs->Serialize(write, resolver) && bs->Serialize(write, attackerNetworkId) && bs->Serialize(write, victimNetworkId) && bs->Serialize(write, victimIncarnation) && bs->Serialize(write, victimEpoch)
                    && bs->Serialize(write, attackGeneration) && bs->Serialize(write, shotSequence) && bs->Serialize(write, healthLoss) && bs->Serialize(write, staminaLoss) && bs->Serialize(write, mayKill) && bs->Serialize(write, reason) && bs->Serialize(write, flags)
                    && bs->Serialize(write, recoilRowIndex) && bs->Serialize(write, hitRowIndex) && bs->Serialize(write, hitRecordFlags) && bs->Serialize(write, hasImpact) && (!hasImpact || impact.Serialize(bs, write)) && bs->Serialize(write, ruledAt)
                    && bs->Serialize(write, reportedAt);
        }
    };
} // namespace KCDC::Shared::RPC
