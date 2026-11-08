#pragma once

#include "features/player/player_missile_module.h"
#include "shared/features/player/player_combat_attack.h"
#include "shared/features/player/player_combat_claim.h"
#include "shared/features/player/player_combat_hit_report.h"
#include "shared/features/player/player_combat_outcome.h"
#include "shared/features/player/player_combat_reaction.h"
#include "shared/features/player/player_combat_report_queue.h"
#include "shared/features/player/player_combat_swing_ledger.h"
#include "shared/features/player/player_entity.h"

#include <mafianet/types.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * Player-versus-player combat's referee.
     *
     * Every swing and every combat action a player's machine ran on its own
     * body passes through here on its way to the peers that hold the body, so
     * the server knows which swings are in the air and how long each can still
     * land. A blow one player lands on another is resolved by the engine on
     * up to two machines, and both say so:
     *
     * - the victim's machine, when its copy of the attacker's blade meets the
     *   real body -- resolved against a guard with no latency in it -- reports
     *   the blow with the health it withheld;
     * - the attacker's machine, when the real blade meets its copy of the
     *   victim, claims it.
     *
     * The victim's report wins. A claim is held until the victim's report of
     * the same swing can have arrived, and ruled on only when none did, so a
     * blow that landed is never lost to a copy that missed. Either is checked
     * first:
     *
     * - the swing must be one this server relayed, still able to land, and
     *   must not have struck this victim already; an arrow must be a shot
     *   whose impact on this victim was accepted, and is paid for once;
     * - the two bodies must stand within a blade's reach of each other;
     * - the gamemode may refuse the blow or change what it takes
     *   (`playerHit`).
     *
     * The ruling goes to both owners and to every peer that holds either body.
     * Each plays what its engine did not already play. One ruling, one order,
     * on every machine.
     */
    class Combat final {
      public:
        void Install(Missile &missiles);

        /** Ends combat between two players, leaving their other opponents alone. */
        bool Cancel(std::uint64_t player, std::uint64_t opponent);
        bool WasCancelled(std::uint64_t player, std::uint64_t opponent, std::uint64_t receivedAt) const;

        /** Rules on every claim whose victim had their chance to report it. */
        void Update();

        void OnPlayerDisconnect(std::uint64_t playerNetworkId);

        /** Relays one player's swing to the peers that hold their body, and opens its window. */
        std::size_t Relay(const KCDC::Shared::RPC::CombatAttack &attack, Framework::Networking::Replication::NetworkEntity &actor);

        /** Relays an action a player's machine ran on its own body; a swing the engine started opens a window too. */
        std::size_t RelayReaction(const KCDC::Shared::RPC::CombatReaction &reaction, Framework::Networking::Replication::NetworkEntity &actor);

        /** How far apart two fighters may stand, in metres, for one's blade to reach the other: a lunge, and the bodies' replication lag. */
        static constexpr float kMaxMeleeReach = 6.0F;

        /** What a victim takes, past a swing's own withdraw, to see the swing, resolve it and say so: a round trip, a frame, the relay. */
        static constexpr std::uint32_t kStrikeSlackMs = 1500;

        /**
         * How long a claim waits for the victim's own report beyond the
         * victim's round trip, and the bounds on the whole wait. The victim's
         * copy starts the swing only once the commit has crossed the relay --
         * the attacker's frame, the server's tick, the victim's frame -- which
         * measured about 270 ms behind the attacker's own contact on one
         * machine; this covers that and its jitter.
         */
        static constexpr std::uint64_t kClaimHoldMarginMs  = 350;
        static constexpr std::uint64_t kClaimHoldMinimumMs = 350;
        static constexpr std::uint64_t kClaimHoldMaximumMs = 900;

        /**
         * How long a claim waits for the victim's answer to the query about
         * it, beyond the victim's round trip, and the bounds on the whole
         * wait. A victim that does not answer in time leaves the claim as
         * made.
         */
        static constexpr std::uint64_t kQueryMarginMs  = 150;
        static constexpr std::uint64_t kQueryMinimumMs = 200;
        static constexpr std::uint64_t kQueryMaximumMs = 700;

      private:
        void OnNpcReaction(const KCDC::Shared::RPC::CombatReaction &reaction, MafiaNet::Packet *packet);
        bool ProcessNpcReport(const KCDC::Shared::RPC::CombatReportQueue::Entry &entry);
        struct NpcSwingContext {
            std::uint32_t incarnation = 0;
            std::uint32_t revision    = 0;
            std::uint64_t sequence    = 0;
        };
        std::unordered_map<std::uint64_t, NpcSwingContext> _npcSwingContexts;

        void OnHitReport(const KCDC::Shared::RPC::CombatHitReport &report, MafiaNet::Packet *packet);
        bool ProcessReport(const KCDC::Shared::RPC::CombatReportQueue::Entry &entry);

        /** Opens `generation`'s window on `attacker`'s ledger. */
        bool OpenSwing(std::uint64_t attackerNetworkId, std::uint32_t generation, std::uint32_t windowMs, std::uint8_t strikesPerVictim = 1, bool counter = false);

        /** Checks a victim's report and rules on it. */
        void OnVictimReport(const KCDC::Shared::RPC::CombatHitReport &report, KCDC::Shared::Entities::HumanEntity &victim);

        /** Checks an attacker's claim and holds it for the victim's report. */
        void OnAttackerClaim(const KCDC::Shared::RPC::CombatHitReport &report, KCDC::Shared::Entities::HumanEntity &attacker);

        /** The victim's account of how it defended against a claimed swing. */
        void OnClaimAnswer(const KCDC::Shared::RPC::CombatClaimAnswer &answer, std::uint64_t senderId);

        /** An attacker's claim, waiting for the victim's own report. */
        struct HeldClaim {
            KCDC::Shared::RPC::CombatHitReport report;
            std::uint64_t ruleAt = 0;

            /** When the claim arrived: as near the contact as the server can date it. */
            std::uint64_t claimedAt = 0;

            /** The victim's life the claim is about: a body that died and came back while it was held is another. */
            std::uint32_t victimIncarnation = 0;
            std::uint8_t victimEpoch        = 0;

            /** Once the victim has been asked about it, the query's id; zero before. */
            std::uint64_t queryId = 0;
        };

        /** Rules on a held claim, as its victim's answer amended it. */
        void RuleClaim(const HeldClaim &claim, std::uint64_t now);

        /** Whether two players can fight each other at all right now. */
        static bool CanFight(const KCDC::Shared::Entities::HumanEntity &attacker, const KCDC::Shared::Entities::HumanEntity &victim);

        /** Whether the bodies stand within reach, by the positions the server holds. */
        static bool WithinReach(const KCDC::Shared::Entities::HumanEntity &attacker, const KCDC::Shared::Entities::HumanEntity &victim);

        /** Lets the gamemode have its say, then sends the ruling; `reportedAt` is when the blow's account arrived. */
        void Rule(const KCDC::Shared::RPC::CombatHitReport &report, KCDC::Shared::Entities::HumanEntity &attacker, KCDC::Shared::Entities::HumanEntity &victim, KCDC::Shared::RPC::CombatHitResolver resolver, std::uint64_t reportedAt);

        /** Sends a ruling to the two owners and to every peer that holds either body. */
        void Broadcast(const KCDC::Shared::RPC::CombatOutcome &outcome, KCDC::Shared::Entities::HumanEntity &attacker, KCDC::Shared::Entities::HumanEntity &victim);

        Missile *_missiles = nullptr;
        KCDC::Shared::RPC::CombatReportQueue _reports;
        std::vector<std::pair<KCDC::Shared::RPC::CombatClaimAnswer, std::uint64_t>> _answers;
        std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> _attacks;
        std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> _cancelledAt;

        // The swings each attacker has in the air, keyed by their NetworkID.
        std::unordered_map<std::uint64_t, KCDC::Shared::RPC::CombatSwingLedger> _ledgers;

        // Keyed by attacker NetworkID, attack generation and victim NetworkID.
        std::map<std::tuple<std::uint64_t, std::uint32_t, std::uint64_t>, HeldClaim> _claims;

        // The claims one Update rules on, taken out of `_claims` first.
        std::vector<HeldClaim> _dueClaims;

        std::uint64_t _lastRuling = 0;
        std::uint64_t _lastQuery  = 0;
    };
} // namespace KCDC::Features::Player
