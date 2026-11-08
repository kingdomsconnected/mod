#pragma once

#include "features/player/player_duel_roster.h"
#include "shared/features/player/player_combat_attack.h"
#include "shared/features/player/player_combat_cancel.h"
#include "shared/features/player/player_combat_claim.h"
#include "shared/features/player/player_combat_hit_report.h"
#include "shared/features/player/player_combat_outcome.h"
#include "shared/features/player/player_combat_reaction.h"
#include "shared/features/player/player_combat_report_queue.h"
#include "shared/features/player/player_combat_swing_ledger.h"

#include <mafianet/bit_stream.h>

#include <limits>

namespace KCDC::Tests::CombatExchange {
    /** A reaction carrying a complete identity fence and a row, and nothing else. */
    inline KCDC::Shared::RPC::CombatReaction Fenced() {
        KCDC::Shared::RPC::CombatReaction reaction;
        reaction.incarnation          = 3;
        reaction.epoch                = 1;
        reaction.combatRevision       = 9;
        reaction.presentationSequence = 42;
        reaction.rowIndex             = 17;
        return reaction;
    }

    /** An engine-started swing: a row, a swing state and its generation. */
    inline KCDC::Shared::RPC::CombatReaction Swing() {
        KCDC::Shared::RPC::CombatReaction reaction = Fenced();
        reaction.kind                              = KCDC::Shared::RPC::CombatReaction::Attack;
        reaction.stateMode                         = 3;
        reaction.attackGeneration                  = 5;
        return reaction;
    }

    /** A victim's report of a swing that landed. */
    inline KCDC::Shared::RPC::CombatHitReport MeleeReport() {
        KCDC::Shared::RPC::CombatHitReport report;
        report.source            = static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatHitSource::Melee);
        report.incarnation       = 2;
        report.epoch             = 1;
        report.attackerNetworkId = 7;
        report.attackGeneration  = 31;
        report.healthLoss        = 14.5F;
        report.staminaLoss       = 22.0F;
        report.hitSubpart        = 3;
        report.reason            = 1;
        return report;
    }

    /** A ruling with everything a receiver checks. */
    inline KCDC::Shared::RPC::CombatOutcome Ruling() {
        KCDC::Shared::RPC::CombatOutcome outcome;
        outcome.sequence          = 1;
        outcome.attackerNetworkId = 7;
        outcome.victimNetworkId   = 8;
        outcome.victimIncarnation = 2;
        outcome.attackGeneration  = 31;
        outcome.healthLoss        = 14.5F;
        return outcome;
    }
} // namespace KCDC::Tests::CombatExchange

MODULE(combat_exchange, {
    IT("publishes counters from NPC authorities and from players against local NPCs", {
        KCDC::Features::Player::DuelRoster<int, int> roster;
        int player = 1, npc = 2, secondNpc = 3, remoteNpc = 4, remotePlayer = 5, vanilla = 6;
        roster.SetLocal(&player, 10);
        roster.AddNpc(&npc, 20, true);
        roster.AddNpc(&secondNpc, 30, true);
        roster.AddNpc(&remoteNpc, 40, false);
        roster.AddRemote(&remotePlayer, 50, &remotePlayer, true);
        EQUALS(roster.CanPublishReaction(&player, &npc), true);
        EQUALS(roster.CanPublishReaction(&npc, &player), true);
        EQUALS(roster.CanPublishReaction(&npc, &secondNpc), true);
        EQUALS(roster.CanPublishReaction(&npc, &remoteNpc), true);
        EQUALS(roster.CanPublishReaction(&npc, &remotePlayer), true);
        EQUALS(roster.CanPublishReaction(&remoteNpc, &npc), false);
        EQUALS(roster.CanPublishReaction(&remotePlayer, &npc), false);
        EQUALS(roster.CanPublishReaction(&npc, &vanilla), false);
        EQUALS(roster.CanPublishReaction(&npc, &npc), false);
    });

    IT("keeps perfect-block rows separate for every fighter and opponent", {
        KCDC::Features::Player::PerfectBlockRows<int> rows;
        int player = 1, npc = 2, other = 3;
        rows.Note(&player, &npc, {0, 5, 0});
        rows.Note(&npc, &player, {27, 31, 1});
        EQUALS(rows.Find(&player, &npc).row, 0U);
        EQUALS(rows.Find(&npc, &player).row, 27U);
        EQUALS(rows.Find(&npc, &other).row, KCDC::Shared::RPC::CombatReaction::kInvalidRowIndex);
        rows.Note(&player, &other, {42, 43, 0});
        EQUALS(rows.Find(&player, &npc).row, KCDC::Shared::RPC::CombatReaction::kInvalidRowIndex);
        EQUALS(rows.Find(&player, &other).row, 42U);
        rows.Retain([&npc](const auto *fighter, const auto *) {
            return fighter == &npc;
        });
        EQUALS(rows.Find(&player, &other).row, KCDC::Shared::RPC::CombatReaction::kInvalidRowIndex);
        EQUALS(rows.Find(&npc, &player).row, 27U);
        rows.Clear();
        EQUALS(rows.Find(&npc, &player).row, KCDC::Shared::RPC::CombatReaction::kInvalidRowIndex);
    });

    IT("hands a riposte the whole perfect block it counters", {
        KCDC::Features::Player::PerfectBlockRows<int> rows;
        int player = 1, attacker = 2;
        rows.Note(&player, &attacker, {27, 31, 1});
        const auto block = rows.Find(&player, &attacker);
        EQUALS(block.row, 27U);
        EQUALS(block.riposteRow, 31U);
        EQUALS(block.candidateFlags, static_cast<std::uint8_t>(1));
        EQUALS(rows.Find(&attacker, &player).riposteRow, KCDC::Shared::RPC::CombatReaction::kInvalidRowIndex);
    });

    IT("resolves replicated NPC contacts only on the victim authority", {
        KCDC::Features::Player::DuelRoster<int, int> roster;
        int player = 1, npc = 2, observer = 3;
        roster.SetLocal(&player, 10);
        roster.AddNpc(&npc, 20, true, 3, 4, 5);
        roster.AddNpc(&observer, 30, false, 7, 8, 9);
        EQUALS(roster.ResolvingAttacker(&npc, &player), &npc);
        EQUALS(roster.ResolvingAttacker(&player, &npc), &player);
        EQUALS(roster.ResolvingAttacker(&observer, &npc), &observer);
        EQUALS(roster.ResolvingAttacker(&npc, &observer) == nullptr, true);
        EQUALS(roster.IsPlayerPair(&npc, &player), true);
        EQUALS(roster.NpcIdentity(&npc)->revision, 5);
        roster.Clear();
        roster.SetLocal(&player, 10);
        roster.AddNpc(&npc, 20, false, 3, 5, 6);
        EQUALS(roster.IsAuthority(&npc), false);
        EQUALS(roster.ResolvingAttacker(&player, &npc) == nullptr, true);
        EQUALS(roster.NpcIdentity(&npc)->revision, 6);
    });

    IT("carries NPC reporter and attacker life fences through the hit report", {
        auto report                = KCDC::Tests::CombatExchange::MeleeReport();
        report.reporterNetworkId   = 41;
        report.reporterRevision    = 42;
        report.attackerIncarnation = 43;
        report.attackerRevision    = 44;
        MafiaNet::BitStream stream;
        report.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatHitReport decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.reporterNetworkId, 41);
        EQUALS(decoded.reporterRevision, 42);
        EQUALS(decoded.attackerIncarnation, 43);
        EQUALS(decoded.attackerRevision, 44);
    });

    IT("rejects incomplete NPC identities and NPC attacker claims", {
        auto report              = KCDC::Tests::CombatExchange::MeleeReport();
        report.reporterNetworkId = 41;
        EQUALS(report.IsSane(), false);
        report.reporterRevision = 9;
        EQUALS(report.IsSane(), true);
        report.attackerIncarnation = 5;
        EQUALS(report.IsSane(), false);
        report.attackerRevision = 6;
        EQUALS(report.IsSane(), true);
        report.resolver        = static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatHitResolver::Attacker);
        report.victimNetworkId = 41;
        EQUALS(report.IsSane(), false);
        report.resolver          = static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatHitResolver::Victim);
        report.victimNetworkId   = 0;
        report.reporterNetworkId = report.attackerNetworkId;
        EQUALS(report.IsSane(), false);
    });

    IT("holds early reports until the presentation fence admits their swing", {
        KCDC::Shared::RPC::CombatReportQueue queue;
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        const auto report = KCDC::Tests::CombatExchange::MeleeReport();
        EQUALS(queue.Push({report, 8, 1000, 3, 1}), true);
        int ruled          = 0;
        const auto process = [&](const auto &entry) {
            const auto result = ledger.Strike(entry.report.attackGeneration, entry.sender, 1200, 1500);
            if (result == KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing) {
                return false;
            }
            if (result == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted) {
                ++ruled;
            }
            return true;
        };
        queue.Drain(1100, process);
        EQUALS(ruled, 0);
        ledger.Relay(report.attackGeneration, 1200, 1000);
        queue.Drain(1200, process);
        queue.Drain(1300, process);
        EQUALS(ruled, 1);
    });

    IT("bounds missing-swing waits and forgets a cancelled pair only", {
        KCDC::Shared::RPC::CombatReportQueue queue;
        const auto report = KCDC::Tests::CombatExchange::MeleeReport();
        for (int i = 0; i < 32; ++i) {
            EQUALS(queue.Push({report, 8, 1000, 3, 1}), true);
        }
        EQUALS(queue.Push({report, 8, 1000, 3, 1}), false);
        int seen = 0;
        queue.Drain(6000, [&](const auto &) {
            ++seen;
            return false;
        });
        EQUALS(seen, 0);
        queue.Push({report, 8, 7000, 3, 1});
        queue.Push({report, 9, 7000, 3, 1});
        queue.Forget(7, 8);
        std::uint64_t remainingSender = 0;
        queue.Drain(7000, [&](const auto &entry) {
            remainingSender = entry.sender;
            ++seen;
            return true;
        });
        EQUALS(seen, 1);
        EQUALS(remainingSender, 9);
    });

    IT("retires a cancelled opponent without retiring a third player or a new swing", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        ledger.Relay(1, 1000, 1000);
        ledger.CancelAgainst(8);
        EQUALS(ledger.Check(1, 8, 1100, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::AlreadyStruck, true);
        EQUALS(ledger.Strike(1, 9, 1100, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        ledger.Relay(2, 1200, 1000);
        EQUALS(ledger.Strike(2, 8, 1300, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
    });

    IT("honours a known open guard and drops the stale block presentation", {
        auto report           = KCDC::Tests::CombatExchange::MeleeReport();
        report.flags          = KCDC::Shared::RPC::CombatHitFlags::kBlocked;
        report.recoilRowIndex = 3;
        report.hasImpact      = true;
        KCDC::Shared::RPC::CombatClaimAnswer answer;
        answer.Amend(report);
        EQUALS(report.flags, KCDC::Shared::RPC::CombatHitFlags::kBlocked);
        answer.known = true;
        answer.Amend(report);
        EQUALS(report.flags, 0);
        EQUALS(report.recoilRowIndex, KCDC::Shared::RPC::CombatHitReport::kNoRow);
        EQUALS(report.hasImpact, false);
        report.hitRowIndex = 4;
        answer.flags       = KCDC::Shared::RPC::CombatHitFlags::kBlocked;
        answer.Amend(report);
        EQUALS(report.hitRowIndex, KCDC::Shared::RPC::CombatHitReport::kNoRow);
    });

    IT("round trips a pair cancellation with both life fences", {
        KCDC::Shared::RPC::CombatCancel command;
        command.first             = 7;
        command.second            = 8;
        command.cancelledAt       = 1500;
        command.firstIncarnation  = 3;
        command.secondIncarnation = 4;
        command.firstEpoch        = 1;
        command.secondEpoch       = 2;
        MafiaNet::BitStream stream;
        command.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatCancel decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.secondIncarnation, 4);
        EQUALS(decoded.secondEpoch, 2);
        decoded.second = decoded.first;
        EQUALS(decoded.IsSane(), false);
    });

    IT("round trips a perfect block with its riposte", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Fenced();
        reaction.kind                              = KCDC::Shared::RPC::CombatReaction::PerfectBlock;
        reaction.opponentNetworkId                 = 5;
        reaction.pairedRowIndex                    = 230;
        reaction.variantFlags                      = 1;
        EQUALS(reaction.IsSane(), true);

        MafiaNet::BitStream stream;
        reaction.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatReaction decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.kind, reaction.kind);
        EQUALS(decoded.opponentNetworkId, reaction.opponentNetworkId);
        EQUALS(decoded.rowIndex, reaction.rowIndex);
        EQUALS(decoded.pairedRowIndex, reaction.pairedRowIndex);
        EQUALS(decoded.variantFlags, reaction.variantFlags);
        EQUALS(decoded.presentationSequence, reaction.presentationSequence);
    });

    IT("needs the other fighter for every kind but a swing, and the paired row for a defense", {
        KCDC::Shared::RPC::CombatReaction block = KCDC::Tests::CombatExchange::Fenced();
        block.kind                              = KCDC::Shared::RPC::CombatReaction::PerfectBlock;
        block.pairedRowIndex                    = 230;
        EQUALS(block.IsSane(), false);
        block.opponentNetworkId = 5;
        EQUALS(block.IsSane(), true);
        block.pairedRowIndex = KCDC::Shared::RPC::CombatReaction::kInvalidRowIndex;
        EQUALS(block.IsSane(), false);

        KCDC::Shared::RPC::CombatReaction hit = KCDC::Tests::CombatExchange::Fenced();
        hit.kind                              = KCDC::Shared::RPC::CombatReaction::Hit;
        EQUALS(hit.IsSane(), false);
        hit.opponentNetworkId = 8;
        EQUALS(hit.IsSane(), true);

        KCDC::Shared::RPC::CombatReaction sync = KCDC::Tests::CombatExchange::Fenced();
        sync.kind                              = KCDC::Shared::RPC::CombatReaction::SyncPerfectBlock;
        sync.opponentNetworkId                 = 8;
        EQUALS(sync.IsSane(), false);
        sync.pairedRowIndex = 3;
        EQUALS(sync.IsSane(), true);
    });

    IT("names an engine-started swing like an input one", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Swing();
        EQUALS(reaction.IsSane(), true);
        reaction.attackGeneration = 0;
        EQUALS(reaction.IsSane(), false);
    });

    IT("rejects an unfenced or rowless reaction and an absurd speed", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Swing();
        EQUALS(reaction.IsSane(), true);

        reaction.attackSpeed = std::numeric_limits<float>::quiet_NaN();
        EQUALS(reaction.IsSane(), false);
        reaction.attackSpeed = 1.25F;
        EQUALS(reaction.IsSane(), true);

        reaction.rowIndex = KCDC::Shared::RPC::CombatReaction::kInvalidRowIndex;
        EQUALS(reaction.IsSane(), false);
        reaction.rowIndex             = 17;
        reaction.presentationSequence = 0;
        EQUALS(reaction.IsSane(), false);
    });

    IT("carries the sender's swing speed and window on a commit", {
        KCDC::Shared::RPC::CombatAttack attack;
        attack.phase                = KCDC::Shared::RPC::CombatAttack::Commit;
        attack.incarnation          = 1;
        attack.combatRevision       = 1;
        attack.presentationSequence = 1;
        attack.attackGeneration     = 1;
        attack.inputClassId         = 4;
        attack.attackRowIndex       = 12;
        attack.stateMode            = 3;
        attack.attackSpeed          = 1.4F;
        attack.windowMs             = 812;
        attack.opponentNetworkId    = 9;
        EQUALS(attack.IsSane(), true);

        MafiaNet::BitStream stream;
        attack.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatAttack decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.attackSpeed, attack.attackSpeed);
        EQUALS(decoded.windowMs, attack.windowMs);
        EQUALS(decoded.opponentNetworkId, attack.opponentNetworkId);

        attack.attackSpeed = KCDC::Shared::RPC::CombatAttack::kMaxAttackSpeed * 2.0F;
        EQUALS(attack.IsSane(), false);
    });

    IT("takes a commit whose sender never saw its speed, and nothing that is not a number", {
        KCDC::Shared::RPC::CombatAttack attack;
        attack.phase                = KCDC::Shared::RPC::CombatAttack::Commit;
        attack.incarnation          = 1;
        attack.combatRevision       = 1;
        attack.presentationSequence = 1;
        attack.attackGeneration     = 1;
        attack.inputClassId         = 4;
        attack.attackRowIndex       = 12;
        attack.stateMode            = 4;
        attack.attackSpeed          = 0.0F;
        EQUALS(attack.IsSane(), true);
        attack.attackSpeed = KCDC::Shared::RPC::CombatAttack::kMaxAttackSpeed;
        EQUALS(attack.IsSane(), true);
        attack.attackSpeed = -0.5F;
        EQUALS(attack.IsSane(), false);
        attack.attackSpeed = std::numeric_limits<float>::quiet_NaN();
        EQUALS(attack.IsSane(), false);
        attack.attackSpeed = std::numeric_limits<float>::infinity();
        EQUALS(attack.IsSane(), false);
    });

    IT("reads a swing's speed only off its commit", {
        KCDC::Shared::RPC::CombatAttack press;
        press.phase                = KCDC::Shared::RPC::CombatAttack::Press;
        press.incarnation          = 1;
        press.combatRevision       = 1;
        press.presentationSequence = 1;
        press.attackGeneration     = 1;
        press.inputClassId         = 4;
        press.attackSpeed          = std::numeric_limits<float>::quiet_NaN();
        EQUALS(press.IsSane(), true);
    });

    IT("asks of the other fighter only what the engine reads of it", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Fenced();
        reaction.kind                              = KCDC::Shared::RPC::CombatReaction::PerfectBlock;
        EQUALS(reaction.RequiredOpponent() == KCDC::Shared::RPC::CombatReaction::OpponentRequirement::CurrentOpponent, true);
        reaction.kind = KCDC::Shared::RPC::CombatReaction::SyncPerfectBlock;
        EQUALS(reaction.RequiredOpponent() == KCDC::Shared::RPC::CombatReaction::OpponentRequirement::CurrentOpponent, true);
        reaction.kind = KCDC::Shared::RPC::CombatReaction::Hit;
        EQUALS(reaction.RequiredOpponent() == KCDC::Shared::RPC::CombatReaction::OpponentRequirement::Present, true);
        reaction.kind = KCDC::Shared::RPC::CombatReaction::Attack;
        EQUALS(reaction.RequiredOpponent() == KCDC::Shared::RPC::CombatReaction::OpponentRequirement::None, true);
        reaction.kind = 200;
        EQUALS(reaction.RequiredOpponent() == KCDC::Shared::RPC::CombatReaction::OpponentRequirement::None, true);
    });

    IT("lets the server refuse a reaction that names no other player in the world", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Fenced();
        reaction.kind                              = KCDC::Shared::RPC::CombatReaction::Hit;
        reaction.opponentNetworkId                 = 2;
        const auto onlyFour                        = [](std::uint64_t id) {
            return id == 4;
        };
        const auto two = [](std::uint64_t id) {
            return id == 2;
        };
        EQUALS(reaction.NamesOnlyOtherPlayers(onlyFour), false);
        EQUALS(reaction.NamesOnlyOtherPlayers(two), true);

        reaction          = KCDC::Tests::CombatExchange::Swing();
        const auto nobody = [](std::uint64_t) {
            return false;
        };
        EQUALS(reaction.NamesOnlyOtherPlayers(nobody), true);
    });

    IT("round trips every kind with every field", {
        for (std::uint8_t kind = KCDC::Shared::RPC::CombatReaction::PerfectBlock; kind <= KCDC::Shared::RPC::CombatReaction::Attack; ++kind) {
            KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Fenced();
            reaction.kind                              = kind;
            reaction.actorNetworkId                    = 21;
            reaction.opponentNetworkId                 = kind == KCDC::Shared::RPC::CombatReaction::Attack ? 0 : 23;
            reaction.pairedRowIndex                    = 99;
            reaction.candidateFlag0                    = 1;
            reaction.candidateFlag1                    = 1;
            reaction.variantFlags                      = 3;
            reaction.contextFlag0                      = 1;
            reaction.contextFlag1                      = 0;
            reaction.stateMode                         = 3;
            reaction.attackSpeed                       = 1.5F;
            reaction.attackGeneration                  = 64;
            reaction.windowMs                          = 700;
            EQUALS(reaction.IsSane(), true);

            MafiaNet::BitStream stream;
            reaction.Serialize(&stream, true);
            KCDC::Shared::RPC::CombatReaction decoded;
            decoded.Serialize(&stream, false);
            EQUALS(decoded.IsSane(), true);
            EQUALS(decoded.kind, reaction.kind);
            EQUALS(decoded.incarnation, reaction.incarnation);
            EQUALS(decoded.epoch, reaction.epoch);
            EQUALS(decoded.combatRevision, reaction.combatRevision);
            EQUALS(decoded.presentationSequence, reaction.presentationSequence);
            EQUALS(decoded.actorNetworkId, reaction.actorNetworkId);
            EQUALS(decoded.opponentNetworkId, reaction.opponentNetworkId);
            EQUALS(decoded.rowIndex, reaction.rowIndex);
            EQUALS(decoded.pairedRowIndex, reaction.pairedRowIndex);
            EQUALS(decoded.candidateFlag0, reaction.candidateFlag0);
            EQUALS(decoded.candidateFlag1, reaction.candidateFlag1);
            EQUALS(decoded.variantFlags, reaction.variantFlags);
            EQUALS(decoded.contextFlag0, reaction.contextFlag0);
            EQUALS(decoded.contextFlag1, reaction.contextFlag1);
            EQUALS(decoded.stateMode, reaction.stateMode);
            EQUALS(decoded.attackSpeed, reaction.attackSpeed);
            EQUALS(decoded.attackGeneration, reaction.attackGeneration);
            EQUALS(decoded.windowMs, reaction.windowMs);
        }
    });

    IT("marks a truncated reaction as broken", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Fenced();
        reaction.kind                              = KCDC::Shared::RPC::CombatReaction::Hit;
        reaction.opponentNetworkId                 = 3;
        MafiaNet::BitStream stream;
        reaction.Serialize(&stream, true);
        MafiaNet::BitStream truncated(stream.GetData(), 8, true);
        KCDC::Shared::RPC::CombatReaction decoded;
        decoded.Serialize(&truncated, false);
        EQUALS(decoded.IsSane(), false);
    });

    IT("rejects out-of-range kinds, flags and swing states", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Swing();
        reaction.stateMode                         = 4;
        EQUALS(reaction.IsSane(), true);

        reaction.stateMode = 5;
        EQUALS(reaction.IsSane(), false);
        reaction.stateMode = 3;

        reaction.candidateFlag0 = 2;
        EQUALS(reaction.IsSane(), false);
        reaction.candidateFlag0 = 0;
        reaction.contextFlag1   = 2;
        EQUALS(reaction.IsSane(), false);
        reaction.contextFlag1 = 0;
        reaction.variantFlags = 4;
        EQUALS(reaction.IsSane(), false);
        reaction.variantFlags = 0;
        reaction.attackSpeed  = -1.0F;
        EQUALS(reaction.IsSane(), false);
        reaction.attackSpeed = 0.0F;
        EQUALS(reaction.IsSane(), true);

        reaction.kind = KCDC::Shared::RPC::CombatReaction::Dodge + 1;
        EQUALS(reaction.IsSane(), false);
        STREQUALS(reaction.KindName(), "Invalid");
    });

    IT("takes a riposte only with its opponent and a generation", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Fenced();
        reaction.kind                              = KCDC::Shared::RPC::CombatReaction::Riposte;
        reaction.opponentNetworkId                 = 4;
        reaction.attackGeneration                  = 0;
        reaction.pairedRowIndex                    = 31;
        EQUALS(reaction.IsSane(), false);
        reaction.attackGeneration = 9;
        EQUALS(reaction.IsSane(), true);
        // The perfect block a receiver may have to rebuild needs its riposte row.
        reaction.pairedRowIndex = KCDC::Shared::RPC::CombatReaction::kInvalidRowIndex;
        EQUALS(reaction.IsSane(), false);
        reaction.pairedRowIndex    = 31;
        reaction.opponentNetworkId = 0;
        EQUALS(reaction.IsSane(), false);
        EQUALS(reaction.RequiredOpponent() == KCDC::Shared::RPC::CombatReaction::OpponentRequirement::CurrentOpponent, true);
    });

    IT("takes a block only against its opponent, with or without the swing's generation", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Fenced();
        reaction.kind                              = KCDC::Shared::RPC::CombatReaction::Block;
        reaction.opponentNetworkId                 = 4;
        EQUALS(reaction.IsSane(), true);
        reaction.attackGeneration = 7;
        EQUALS(reaction.IsSane(), true);
        reaction.opponentNetworkId = 0;
        EQUALS(reaction.IsSane(), false);
        EQUALS(reaction.RequiredOpponent() == KCDC::Shared::RPC::CombatReaction::OpponentRequirement::CurrentOpponent, true);
    });

    IT("round trips a dodge's direction and refuses one that is not a direction", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Fenced();
        reaction.kind                              = KCDC::Shared::RPC::CombatReaction::Dodge;
        reaction.opponentNetworkId                 = 4;
        reaction.direction[0]                      = -0.6F;
        reaction.direction[1]                      = 0.8F;
        EQUALS(reaction.IsSane(), true);
        EQUALS(reaction.RequiredOpponent() == KCDC::Shared::RPC::CombatReaction::OpponentRequirement::CurrentOpponent, true);
        MafiaNet::BitStream stream;
        reaction.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatReaction decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.direction[0] == -0.6F && decoded.direction[1] == 0.8F, true);

        reaction.direction[0] = std::numeric_limits<float>::quiet_NaN();
        EQUALS(reaction.IsSane(), false);
        reaction.direction[0] = 3.0F;
        EQUALS(reaction.IsSane(), false);
    });

    IT("names every kind", {
        KCDC::Shared::RPC::CombatReaction reaction;
        const char *names[] = {"PerfectBlock", "SyncPerfectBlock", "Hit", "Attack", "Riposte", "Block", "Dodge"};
        for (std::uint8_t kind = 0; kind < 7; ++kind) {
            reaction.kind = kind;
            STREQUALS(reaction.KindName(), names[kind]);
        }
    });

    IT("refuses an unfenced reaction however complete the rest", {
        KCDC::Shared::RPC::CombatReaction reaction = KCDC::Tests::CombatExchange::Fenced();
        reaction.kind                              = KCDC::Shared::RPC::CombatReaction::Hit;
        reaction.opponentNetworkId                 = 3;
        EQUALS(reaction.IsSane(), true);
        reaction.incarnation = 0;
        EQUALS(reaction.IsSane(), false);
        reaction.incarnation    = 3;
        reaction.combatRevision = 0;
        EQUALS(reaction.IsSane(), false);
        reaction.combatRevision = 9;
        reaction.valid          = false;
        EQUALS(reaction.IsSane(), false);
    });
});

MODULE(combat_referee, {
    IT("round trips a melee report with the impact of its contact", {
        KCDC::Shared::RPC::CombatHitReport report = KCDC::Tests::CombatExchange::MeleeReport();
        report.flags                              = KCDC::Shared::RPC::CombatHitFlags::kBlocked | KCDC::Shared::RPC::CombatHitFlags::kBlockBroken;
        report.attackTypeId                       = 12;
        report.recoilRowIndex                     = 40;
        report.engineLethal                       = true;
        report.hasImpact                          = true;
        report.impact.weaponKind                  = 1;
        report.impact.passed                      = true;
        report.impact.bodyMaterialId              = 7;
        report.impact.attackerJointId             = 57;
        report.impact.offset[2]                   = 1.4F;
        EQUALS(report.IsSane(), true);

        MafiaNet::BitStream stream;
        report.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatHitReport decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.Source() == KCDC::Shared::RPC::CombatHitSource::Melee, true);
        EQUALS(decoded.attackerNetworkId, report.attackerNetworkId);
        EQUALS(decoded.attackGeneration, report.attackGeneration);
        EQUALS(decoded.healthLoss, report.healthLoss);
        EQUALS(decoded.staminaLoss, report.staminaLoss);
        EQUALS(decoded.engineLethal, true);
        EQUALS(decoded.hitSubpart, report.hitSubpart);
        EQUALS(decoded.flags, report.flags);
        EQUALS(decoded.attackTypeId, report.attackTypeId);
        EQUALS(decoded.recoilRowIndex, report.recoilRowIndex);
        EQUALS(decoded.hasImpact, true);
        EQUALS(decoded.impact.attackerJointId, report.impact.attackerJointId);
        EQUALS(decoded.impact.offset[2], report.impact.offset[2]);
    });

    IT("names a swing by its generation and an arrow by its shot", {
        KCDC::Shared::RPC::CombatHitReport report = KCDC::Tests::CombatExchange::MeleeReport();
        report.attackGeneration                   = 0;
        EQUALS(report.IsSane(), false);

        report.source       = static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatHitSource::Missile);
        report.shotSequence = 0;
        EQUALS(report.IsSane(), false);
        report.shotSequence = 900;
        EQUALS(report.IsSane(), true);

        // An arrow is never blocked by a blade nor recoiled from.
        report.flags = KCDC::Shared::RPC::CombatHitFlags::kBlocked;
        EQUALS(report.IsSane(), false);
        report.flags          = 0;
        report.recoilRowIndex = 3;
        EQUALS(report.IsSane(), false);
    });

    IT("refuses a report with no attacker, no life, or impossible numbers", {
        KCDC::Shared::RPC::CombatHitReport report = KCDC::Tests::CombatExchange::MeleeReport();
        report.attackerNetworkId                  = 0;
        EQUALS(report.IsSane(), false);
        report             = KCDC::Tests::CombatExchange::MeleeReport();
        report.incarnation = 0;
        EQUALS(report.IsSane(), false);
        report            = KCDC::Tests::CombatExchange::MeleeReport();
        report.healthLoss = -1.0F;
        EQUALS(report.IsSane(), false);
        report.healthLoss = std::numeric_limits<float>::quiet_NaN();
        EQUALS(report.IsSane(), false);
        report.healthLoss = KCDC::Shared::RPC::CombatHitReport::kMaxHealthLoss * 2.0F;
        EQUALS(report.IsSane(), false);
        report             = KCDC::Tests::CombatExchange::MeleeReport();
        report.staminaLoss = std::numeric_limits<float>::infinity();
        EQUALS(report.IsSane(), false);
        report       = KCDC::Tests::CombatExchange::MeleeReport();
        report.flags = 0x80;
        EQUALS(report.IsSane(), false);
        report        = KCDC::Tests::CombatExchange::MeleeReport();
        report.source = 9;
        EQUALS(report.IsSane(), false);
    });

    IT("marks a truncated report as broken", {
        KCDC::Shared::RPC::CombatHitReport report = KCDC::Tests::CombatExchange::MeleeReport();
        MafiaNet::BitStream stream;
        report.Serialize(&stream, true);
        MafiaNet::BitStream truncated(stream.GetData(), 10, true);
        KCDC::Shared::RPC::CombatHitReport decoded;
        decoded.Serialize(&truncated, false);
        EQUALS(decoded.IsSane(), false);
    });

    IT("round trips a ruling", {
        KCDC::Shared::RPC::CombatOutcome outcome = KCDC::Tests::CombatExchange::Ruling();
        outcome.mayKill                          = true;
        outcome.reason                           = 1;
        outcome.flags                            = KCDC::Shared::RPC::CombatHitFlags::kPerfectBlock;
        outcome.recoilRowIndex                   = 12;
        outcome.ruledAt                          = 123456;
        EQUALS(outcome.IsSane(), true);

        MafiaNet::BitStream stream;
        outcome.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatOutcome decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.sequence, outcome.sequence);
        EQUALS(decoded.attackerNetworkId, outcome.attackerNetworkId);
        EQUALS(decoded.victimNetworkId, outcome.victimNetworkId);
        EQUALS(decoded.victimIncarnation, outcome.victimIncarnation);
        EQUALS(decoded.healthLoss, outcome.healthLoss);
        EQUALS(decoded.mayKill, true);
        EQUALS(decoded.flags, outcome.flags);
        EQUALS(decoded.recoilRowIndex, outcome.recoilRowIndex);
        EQUALS(static_cast<std::uint64_t>(decoded.ruledAt), static_cast<std::uint64_t>(outcome.ruledAt));
    });

    IT("refuses a ruling about nobody, or about a body ruling on itself", {
        KCDC::Shared::RPC::CombatOutcome outcome = KCDC::Tests::CombatExchange::Ruling();
        outcome.sequence                         = 0;
        EQUALS(outcome.IsSane(), false);
        outcome                 = KCDC::Tests::CombatExchange::Ruling();
        outcome.victimNetworkId = outcome.attackerNetworkId;
        EQUALS(outcome.IsSane(), false);
        outcome                   = KCDC::Tests::CombatExchange::Ruling();
        outcome.victimIncarnation = 0;
        EQUALS(outcome.IsSane(), false);
        outcome            = KCDC::Tests::CombatExchange::Ruling();
        outcome.healthLoss = -2.0F;
        EQUALS(outcome.IsSane(), false);
    });

    IT("lets a relayed swing strike each victim once, within its window", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        ledger.Relay(31, 1000, 800);
        EQUALS(ledger.Strike(31, 8, 1500, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        EQUALS(ledger.Strike(31, 8, 1600, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::AlreadyStruck, true);
        EQUALS(ledger.Strike(31, 9, 3200, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        EQUALS(ledger.Strike(31, 10, 3400, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::Expired, true);
        EQUALS(ledger.Strike(32, 8, 1500, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing, true);
        EQUALS(ledger.Strike(0, 8, 1500, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing, true);
    });

    IT("refuses a blow reported before its swing was relayed", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        ledger.Relay(4, 5000, 600);
        EQUALS(ledger.Strike(4, 8, 4999, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::Expired, true);
        EQUALS(ledger.Strike(4, 8, 5000, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
    });

    IT("bounds how many a swing strikes and how long it can claim", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        ledger.Relay(1, 0, 1000000);
        EQUALS(ledger.swings[0].windowMs, KCDC::Shared::RPC::CombatSwingLedger::kMaxWindowMs);
        for (std::uint64_t victim = 1; victim <= KCDC::Shared::RPC::CombatSwingLedger::kMaxVictims; ++victim) {
            EQUALS(ledger.Strike(1, victim, 10, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        }
        EQUALS(ledger.Strike(1, 99, 10, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::TooManyVictims, true);
    });

    IT("keeps the newest swings and forgets the oldest", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        constexpr std::uint32_t newest = KCDC::Shared::RPC::CombatSwingLedger::kMaxSwings + 1;
        for (std::uint32_t generation = 1; generation <= newest; ++generation) {
            EQUALS(ledger.Relay(generation, generation * 200ULL, 1000), true);
        }
        EQUALS(ledger.Strike(1, 8, newest * 200ULL, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing, true);
        EQUALS(ledger.Strike(newest, 8, newest * 200ULL, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
    });

    IT("never opens a swing it already holds, so a resent one cannot strike again", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        EQUALS(ledger.Relay(7, 0, 100), true);
        EQUALS(ledger.Strike(7, 8, 50, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        EQUALS(ledger.Relay(7, 60, 100), false);
        EQUALS(ledger.Strike(7, 8, 70, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::AlreadyStruck, true);
        EQUALS(ledger.Relay(0, 80, 100), false);
    });

    IT("knows which swings are a riposte's strike-back", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        EQUALS(ledger.Relay(3, 0, 800), true);
        EQUALS(ledger.Relay(4, 10, 800, 1, true), true);
        EQUALS(ledger.IsCounter(3), false);
        EQUALS(ledger.IsCounter(4), true);
        EQUALS(ledger.IsCounter(5), false);
    });

    IT("opens no more swings a second than a fighter can throw", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        for (std::uint32_t generation = 1; generation <= KCDC::Shared::RPC::CombatSwingLedger::kMaxSwingsPerSecond; ++generation) {
            EQUALS(ledger.Relay(generation, generation, 1000), true);
        }
        EQUALS(ledger.Relay(100, 500, 1000), false);
        EQUALS(ledger.Strike(100, 8, 600, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing, true);
        EQUALS(ledger.Relay(100, 1500, 1000), true);
    });

    IT("names a claim's victim, and keeps a report's on its sender", {
        KCDC::Shared::RPC::CombatHitReport claim = KCDC::Tests::CombatExchange::MeleeReport();
        claim.resolver                           = static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatHitResolver::Attacker);
        EQUALS(claim.IsClaim(), true);
        EQUALS(claim.IsSane(), false);
        claim.victimNetworkId = 8;
        EQUALS(claim.IsSane(), true);
        claim.victimNetworkId = claim.attackerNetworkId;
        EQUALS(claim.IsSane(), false);
        claim.victimNetworkId = 8;

        // A claim is a blade's; an arrow's hit is the shooter's already accepted impact.
        claim.source       = static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatHitSource::Missile);
        claim.shotSequence = 3;
        EQUALS(claim.IsSane(), false);

        KCDC::Shared::RPC::CombatHitReport report = KCDC::Tests::CombatExchange::MeleeReport();
        report.victimNetworkId                    = 8;
        EQUALS(report.IsSane(), false);
        report.victimNetworkId = 0;
        report.hitRowIndex     = 4;
        EQUALS(report.IsSane(), false);
        report.hitRowIndex = KCDC::Shared::RPC::CombatHitReport::kNoRow;
        report.resolver    = 7;
        EQUALS(report.IsSane(), false);
    });

    IT("round trips a claim with the reaction it ran on the copy", {
        KCDC::Shared::RPC::CombatHitReport claim = KCDC::Tests::CombatExchange::MeleeReport();
        claim.resolver                           = static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatHitResolver::Attacker);
        claim.victimNetworkId                    = 8;
        claim.hitRowIndex                        = 77;
        claim.hitRecordFlags                     = 2;
        EQUALS(claim.IsSane(), true);

        MafiaNet::BitStream stream;
        claim.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatHitReport decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.IsClaim(), true);
        EQUALS(decoded.victimNetworkId, claim.victimNetworkId);
        EQUALS(decoded.hitRowIndex, claim.hitRowIndex);
        EQUALS(decoded.hitRecordFlags, claim.hitRecordFlags);

        claim.hitRecordFlags = 4;
        EQUALS(claim.IsSane(), false);
    });

    IT("round trips who resolved a ruling, and what it leaves the victim to play", {
        KCDC::Shared::RPC::CombatOutcome outcome = KCDC::Tests::CombatExchange::Ruling();
        outcome.resolver                         = static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatHitResolver::Attacker);
        outcome.staminaLoss                      = 21.5F;
        outcome.hitRowIndex                      = 77;
        outcome.hitRecordFlags                   = 1;
        EQUALS(outcome.IsSane(), true);

        MafiaNet::BitStream stream;
        outcome.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatOutcome decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.Resolver() == KCDC::Shared::RPC::CombatHitResolver::Attacker, true);
        EQUALS(decoded.staminaLoss, outcome.staminaLoss);
        EQUALS(decoded.hitRowIndex, outcome.hitRowIndex);
        EQUALS(decoded.hitRecordFlags, outcome.hitRecordFlags);

        outcome.staminaLoss = -1.0F;
        EQUALS(outcome.IsSane(), false);
        outcome.staminaLoss = 0.0F;
        outcome.resolver    = 2;
        EQUALS(outcome.IsSane(), false);
    });

    IT("checks a claim against the ledger without spending the swing", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        ledger.Relay(9, 1000, 600);
        EQUALS(ledger.Check(9, 8, 1200, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        EQUALS(ledger.Check(9, 8, 1200, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        EQUALS(ledger.Strike(9, 8, 1300, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        EQUALS(ledger.Check(9, 8, 1400, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::AlreadyStruck, true);
        EQUALS(ledger.Check(10, 8, 1400, 1500) == KCDC::Shared::RPC::CombatSwingLedger::Result::UnknownSwing, true);
    });

    IT("sizes a swing's window off its own row and speed", {
        // 0.6 s to the hit at speed 1.2 is 0.5 s, then 0.25 s to withdraw.
        EQUALS(KCDC::Shared::RPC::CombatSwingWindowMs(0.6F, 0.85F, 1.2F), static_cast<std::uint32_t>(750));
        // An unknown speed is the authored one.
        EQUALS(KCDC::Shared::RPC::CombatSwingWindowMs(0.6F, 0.85F, 0.0F), static_cast<std::uint32_t>(850));
        // A withdraw authored before the hit adds nothing.
        EQUALS(KCDC::Shared::RPC::CombatSwingWindowMs(0.5F, 0.2F, 1.0F), static_cast<std::uint32_t>(500));
        EQUALS(KCDC::Shared::RPC::CombatSwingWindowMs(std::numeric_limits<float>::quiet_NaN(), 1.0F, 1.0F), static_cast<std::uint32_t>(0));
        EQUALS(KCDC::Shared::RPC::CombatSwingWindowMs(100.0F, 200.0F, 1.0F), KCDC::Shared::RPC::CombatSwingLedger::kMaxWindowMs);
    });
});

MODULE(combat_guard_history, {
    IT("knows nothing of a swing it never played", {
        KCDC::Features::Player::CombatGuardHistory history;
        const KCDC::Shared::RPC::CombatClaimAnswer answer = history.Judge(7, 3);
        EQUALS(answer.known, false);
        EQUALS(answer.flags, 0);
    });

    IT("calls a swing met by a held block blocked, and an open guard taken", {
        KCDC::Features::Player::CombatGuardHistory history;
        history.NoteSwing(1000, 7, 3, 1000);
        history.NoteBlocking(1500, true);
        history.NoteBlocking(1900, false);
        EQUALS(history.Judge(7, 3).flags, KCDC::Shared::RPC::CombatHitFlags::kBlocked);

        history.NoteSwing(3000, 7, 4, 1000);
        EQUALS(history.Judge(7, 4).known, true);
        EQUALS(history.Judge(7, 4).flags, 0);
    });

    IT("counts a block already up when the blade could first land, but not one dropped before", {
        KCDC::Features::Player::CombatGuardHistory history;
        history.NoteBlocking(500, true);
        history.NoteSwing(1000, 7, 3, 1000);
        EQUALS(history.Judge(7, 3).flags, KCDC::Shared::RPC::CombatHitFlags::kBlocked);

        KCDC::Features::Player::CombatGuardHistory dropped;
        dropped.NoteBlocking(500, true);
        dropped.NoteBlocking(1100, false);
        dropped.NoteSwing(1000, 7, 3, 1000);
        // Down at 1100, before the earliest landing at 1350.
        EQUALS(dropped.Judge(7, 3).flags, 0);
    });

    IT("calls a perfect block against the attacker during the swing perfect, and ignores one against another", {
        KCDC::Features::Player::CombatGuardHistory history;
        history.NoteSwing(1000, 7, 3, 1000);
        history.NotePerfectBlock(1200, 9);
        EQUALS(history.Judge(7, 3).flags, 0);
        history.NotePerfectBlock(1250, 7);
        EQUALS(history.Judge(7, 3).flags, (KCDC::Shared::RPC::CombatHitFlags::kBlocked | KCDC::Shared::RPC::CombatHitFlags::kPerfectBlock));
    });

    IT("forgets old swings but keeps the block state that still holds", {
        KCDC::Features::Player::CombatGuardHistory history;
        history.NoteBlocking(100, true);
        history.NoteSwing(200, 7, 3, 500);
        history.Trim(200 + KCDC::Features::Player::CombatGuardHistory::kKeepMs + 1);
        EQUALS(history.Judge(7, 3).known, false);
        history.NoteSwing(10000, 7, 4, 500);
        EQUALS(history.Judge(7, 4).flags, KCDC::Shared::RPC::CombatHitFlags::kBlocked);
    });

    IT("lets a master strike land several contacts on one victim and counts them", {
        KCDC::Shared::RPC::CombatSwingLedger ledger;
        ledger.Relay(5, 1000, 2000, KCDC::Shared::RPC::CombatSwingLedger::kSyncStrikesPerVictim);
        ledger.Relay(6, 1000, 2000);
        EQUALS(ledger.StrikesOn(5, 7), 0);
        for (std::uint8_t i = 0; i < KCDC::Shared::RPC::CombatSwingLedger::kSyncStrikesPerVictim; ++i) {
            EQUALS(ledger.Strike(5, 7, 1100, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        }
        EQUALS(ledger.Strike(5, 7, 1100, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::AlreadyStruck, true);
        EQUALS(ledger.StrikesOn(5, 7), KCDC::Shared::RPC::CombatSwingLedger::kSyncStrikesPerVictim);
        EQUALS(ledger.Strike(6, 7, 1100, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::Accepted, true);
        EQUALS(ledger.Strike(6, 7, 1100, 0) == KCDC::Shared::RPC::CombatSwingLedger::Result::AlreadyStruck, true);
        EQUALS(ledger.StrikesOn(99, 7), 0);
    });

    IT("round trips a query and an answer and refuses a perfect block without a block", {
        KCDC::Shared::RPC::CombatClaimAnswer answer;
        answer.queryId = 5;
        answer.known   = true;
        answer.flags   = KCDC::Shared::RPC::CombatHitFlags::kBlocked | KCDC::Shared::RPC::CombatHitFlags::kPerfectBlock;
        MafiaNet::BitStream stream;
        answer.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatClaimAnswer decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.flags, answer.flags);
        decoded.flags = KCDC::Shared::RPC::CombatHitFlags::kPerfectBlock;
        EQUALS(decoded.IsSane(), false);
        decoded.flags = KCDC::Shared::RPC::CombatHitFlags::kBlocked;
        decoded.known = false;
        EQUALS(decoded.IsSane(), false);
    });
});
