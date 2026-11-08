#pragma once

#include "shared/features/door/door_entity.h"
#include "shared/features/door/door_policy.h"
#include "shared/features/door/door_rpc.h"
#include "shared/features/door/gate_entity.h"
#include "shared/features/player/player_combat_attack.h"
#include "shared/game/door_catalog.h"

#include <mafianet/bit_stream.h>

#include <cmath>
#include <limits>
#include <string>

MODULE(door_protocol, {
    IT("round trips a causally fenced combat attack", {
        KCDC::Shared::RPC::CombatAttack attack;
        attack.phase                = KCDC::Shared::RPC::CombatAttack::Commit;
        attack.incarnation          = 7;
        attack.epoch                = 0;
        attack.combatRevision       = 19;
        attack.presentationSequence = 12;
        attack.attackGeneration     = 5;
        attack.inputClassId         = 4;
        attack.zoneId               = 2;
        attack.attackRowIndex       = 71;
        attack.candidateFlag0       = 1;
        attack.candidateFlag1       = 0;
        attack.contextFlag0         = 0;
        attack.contextFlag1         = 1;
        attack.stateMode            = 4;
        EQUALS(attack.IsSane(), true);

        MafiaNet::BitStream stream;
        attack.Serialize(&stream, true);
        KCDC::Shared::RPC::CombatAttack decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.phase, attack.phase);
        EQUALS(decoded.incarnation, attack.incarnation);
        EQUALS(decoded.epoch, attack.epoch);
        EQUALS(decoded.combatRevision, attack.combatRevision);
        EQUALS(decoded.presentationSequence, attack.presentationSequence);
        EQUALS(decoded.attackGeneration, attack.attackGeneration);
        EQUALS(decoded.inputClassId, attack.inputClassId);
        EQUALS(decoded.zoneId, attack.zoneId);
        EQUALS(decoded.attackRowIndex, attack.attackRowIndex);
        EQUALS(decoded.candidateFlag0, attack.candidateFlag0);
        EQUALS(decoded.candidateFlag1, attack.candidateFlag1);
        EQUALS(decoded.contextFlag0, attack.contextFlag0);
        EQUALS(decoded.contextFlag1, attack.contextFlag1);
        EQUALS(decoded.stateMode, attack.stateMode);
    });

    IT("rejects incomplete combat presentation fences", {
        KCDC::Shared::RPC::CombatAttack attack;
        attack.incarnation          = 1;
        attack.combatRevision       = 1;
        attack.presentationSequence = 1;
        attack.attackGeneration     = 1;
        attack.inputClassId         = 1;
        EQUALS(attack.IsSane(), true);
        attack.incarnation = 0;
        EQUALS(attack.IsSane(), false);
        attack.incarnation    = 1;
        attack.combatRevision = 0;
        EQUALS(attack.IsSane(), false);
        attack.combatRevision       = 1;
        attack.presentationSequence = 0;
        EQUALS(attack.IsSane(), false);
        attack.presentationSequence = 1;
        attack.attackGeneration     = 0;
        EQUALS(attack.IsSane(), false);
        attack.attackGeneration = 1;
        attack.phase            = 3;
        EQUALS(attack.IsSane(), false);

        attack.phase        = KCDC::Shared::RPC::CombatAttack::Press;
        attack.inputClassId = -1;
        EQUALS(attack.IsSane(), false);
        attack.phase = KCDC::Shared::RPC::CombatAttack::Commit;
        EQUALS(attack.IsSane(), false);
        attack.inputClassId   = 1;
        attack.attackRowIndex = 8;
        attack.stateMode      = 3;
        EQUALS(attack.IsSane(), true);
        attack.stateMode = 4;
        EQUALS(attack.IsSane(), true);
        attack.stateMode = 2;
        EQUALS(attack.IsSane(), false);
        attack.stateMode      = 3;
        attack.candidateFlag0 = 2;
        EQUALS(attack.IsSane(), false);
        attack.candidateFlag0 = 0;
        attack.candidateFlag1 = 2;
        EQUALS(attack.IsSane(), false);
        attack.candidateFlag1 = 0;
        attack.contextFlag0   = 2;
        EQUALS(attack.IsSane(), false);
        attack.contextFlag0 = 0;
        attack.contextFlag1 = 2;
        EQUALS(attack.IsSane(), false);
        attack.contextFlag1 = 0;
        EQUALS(attack.IsSane(), true);
        attack.attackRowIndex = KCDC::Shared::RPC::CombatAttack::kInvalidAttackRowIndex;
        EQUALS(attack.IsSane(), false);
        attack.phase        = KCDC::Shared::RPC::CombatAttack::Cancel;
        attack.inputClassId = -1;
        EQUALS(attack.IsSane(), true);

        KCDC::Shared::RPC::CombatAttack complete;
        complete.incarnation          = 1;
        complete.combatRevision       = 1;
        complete.presentationSequence = 1;
        complete.attackGeneration     = 1;
        complete.inputClassId         = 1;
        MafiaNet::BitStream full;
        complete.Serialize(&full, true);
        MafiaNet::BitStream truncated(full.GetData(), full.GetNumberOfBytesUsed() - 1, true);
        KCDC::Shared::RPC::CombatAttack decoded;
        decoded.Serialize(&truncated, false);
        EQUALS(decoded.IsSane(), false);
    });

    IT("enforces one accepted combat attack generation per sender", {
        KCDC::Shared::RPC::CombatAttackLifecycle lifecycle;
        KCDC::Shared::RPC::CombatAttack attack;
        attack.incarnation          = 1;
        attack.combatRevision       = 1;
        attack.presentationSequence = 1;
        attack.attackGeneration     = 7;
        attack.inputClassId         = 1;

        EQUALS(static_cast<std::uint8_t>(lifecycle.Accept(attack)), static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatAttackLifecycle::Result::Accepted));
        EQUALS(lifecycle.activeGeneration, 7U);
        EQUALS(static_cast<std::uint8_t>(lifecycle.Accept(attack)), static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatAttackLifecycle::Result::ActiveGenerationExists));

        attack.phase            = KCDC::Shared::RPC::CombatAttack::Commit;
        attack.attackGeneration = 8;
        attack.attackRowIndex   = 9;
        EQUALS(static_cast<std::uint8_t>(lifecycle.Accept(attack)), static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatAttackLifecycle::Result::TerminalGenerationMismatch));
        attack.attackGeneration = 7;
        EQUALS(static_cast<std::uint8_t>(lifecycle.Accept(attack)), static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatAttackLifecycle::Result::Accepted));
        EQUALS(lifecycle.activeGeneration, 0U);
        EQUALS(static_cast<std::uint8_t>(lifecycle.Accept(attack)), static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatAttackLifecycle::Result::TerminalWithoutPress));

        attack.phase            = KCDC::Shared::RPC::CombatAttack::Press;
        attack.attackGeneration = 8;
        EQUALS(static_cast<std::uint8_t>(lifecycle.Accept(attack)), static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatAttackLifecycle::Result::Accepted));
        attack.phase = KCDC::Shared::RPC::CombatAttack::Cancel;
        EQUALS(static_cast<std::uint8_t>(lifecycle.Accept(attack)), static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatAttackLifecycle::Result::Accepted));
        attack.phase            = KCDC::Shared::RPC::CombatAttack::Press;
        attack.attackGeneration = 9;
        EQUALS(static_cast<std::uint8_t>(lifecycle.Accept(attack)), static_cast<std::uint8_t>(KCDC::Shared::RPC::CombatAttackLifecycle::Result::Accepted));
        EQUALS(lifecycle.activeGeneration, 9U);
        lifecycle.Reset();
        EQUALS(lifecycle.activeGeneration, 0U);
    });

    IT("names a level distinctly and never as zero", {
        EQUALS(KCDC::Shared::RPC::DoorLevelHash("levels/rataje") == 0U, false);
        EQUALS(KCDC::Shared::RPC::DoorLevelHash("levels/rataje") == KCDC::Shared::RPC::DoorLevelHash("levels/trosky"), false);
        EQUALS(KCDC::Shared::RPC::DoorLevelHash("levels/rataje"), KCDC::Shared::RPC::DoorLevelHash("levels/rataje"));
        EQUALS(KCDC::Shared::RPC::DoorLevelHash("") == 0U, false);
    });

    IT("maps every actor action to its native name and back", {
        for (std::uint8_t candidate = 1; candidate <= static_cast<std::uint8_t>(KCDC::Shared::RPC::DoorActorAction::RightFrontOpen); ++candidate) {
            const KCDC::Shared::RPC::DoorActorAction action = static_cast<KCDC::Shared::RPC::DoorActorAction>(candidate);
            const char *name                                 = KCDC::Shared::RPC::DoorActorActionName(action);
            EQUALS(name != nullptr, true);
            EQUALS(static_cast<std::uint8_t>(KCDC::Shared::RPC::DoorActorActionFromName(name)), candidate);
        }
        EQUALS(KCDC::Shared::RPC::DoorActorActionName(KCDC::Shared::RPC::DoorActorAction::None) == nullptr, true);
        EQUALS(static_cast<std::uint8_t>(KCDC::Shared::RPC::DoorActorActionFromName(nullptr)), static_cast<std::uint8_t>(KCDC::Shared::RPC::DoorActorAction::None));
        EQUALS(static_cast<std::uint8_t>(KCDC::Shared::RPC::DoorActorActionFromName("arbitrary_interaction")), static_cast<std::uint8_t>(KCDC::Shared::RPC::DoorActorAction::None));
    });

    IT("round trips a manipulation with its actor fence", {
        KCDC::Shared::RPC::DoorInteraction interaction;
        interaction.objectAnimation      = "open_front";
        interaction.doorGuid             = 9;
        interaction.levelHash            = KCDC::Shared::RPC::DoorLevelHash("levels/rataje");
        interaction.presentationSequence = 31;
        interaction.actorNetworkId       = 33;
        interaction.actorIncarnation     = 4;
        interaction.actorStateEpoch      = 2;
        interaction.actorCombatRevision  = 12;
        interaction.event                = KCDC::Shared::RPC::DoorEvent::Manipulation;
        interaction.actorAction          = KCDC::Shared::RPC::DoorActorAction::RightBackClose;
        interaction.open                 = true;
        interaction.fromFront            = true;
        interaction.copyWorld            = 5;
        EQUALS(interaction.IsSane(), true);

        MafiaNet::BitStream stream;
        interaction.Serialize(&stream, true);
        KCDC::Shared::RPC::DoorInteraction decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.copyWorld, std::uint32_t {5});
        EQUALS(decoded.IsSane(), true);
        STREQUALS(decoded.objectAnimation.c_str(), interaction.objectAnimation.c_str());
        EQUALS(decoded.doorGuid, interaction.doorGuid);
        EQUALS(decoded.levelHash, interaction.levelHash);
        EQUALS(decoded.presentationSequence, interaction.presentationSequence);
        EQUALS(decoded.actorNetworkId, interaction.actorNetworkId);
        EQUALS(decoded.actorIncarnation, interaction.actorIncarnation);
        EQUALS(decoded.actorStateEpoch, interaction.actorStateEpoch);
        EQUALS(decoded.actorCombatRevision, interaction.actorCombatRevision);
        EQUALS(static_cast<std::uint8_t>(decoded.actorAction), static_cast<std::uint8_t>(interaction.actorAction));
        EQUALS(decoded.open, true);
        EQUALS(decoded.fromFront, true);
    });

    IT("carries the two animations only on a manipulation", {
        KCDC::Shared::RPC::DoorInteraction interaction;
        interaction.doorGuid             = 1;
        interaction.levelHash            = 1;
        interaction.presentationSequence = 1;
        interaction.actorIncarnation     = 1;
        interaction.actorCombatRevision  = 1;
        interaction.event                = KCDC::Shared::RPC::DoorEvent::Manipulation;
        interaction.actorAction          = KCDC::Shared::RPC::DoorActorAction::LeftFrontOpen;
        interaction.objectAnimation      = "open_front";
        EQUALS(interaction.IsSane(), true);

        // A manipulation that moves a leaf must say how.
        interaction.actorAction = KCDC::Shared::RPC::DoorActorAction::None;
        EQUALS(interaction.IsSane(), false);
        interaction.actorAction = KCDC::Shared::RPC::DoorActorAction::LeftFrontOpen;
        interaction.objectAnimation.clear();
        EQUALS(interaction.IsSane(), false);
        interaction.objectAnimation = "open_front";

        // A lockpick moves no leaf, so it may carry neither.
        interaction.event = KCDC::Shared::RPC::DoorEvent::Lockpicked;
        EQUALS(interaction.IsSane(), false);
        interaction.actorAction = KCDC::Shared::RPC::DoorActorAction::None;
        EQUALS(interaction.IsSane(), false);
        interaction.objectAnimation.clear();
        EQUALS(interaction.IsSane(), true);
    });

    IT("rejects invalid identities, sequences, events and lockpick results", {
        KCDC::Shared::RPC::DoorInteraction interaction;
        interaction.doorGuid             = 1;
        interaction.levelHash            = 1;
        interaction.presentationSequence = 1;
        interaction.actorIncarnation     = 1;
        interaction.actorCombatRevision  = 1;
        interaction.event                = KCDC::Shared::RPC::DoorEvent::Lockpicked;
        EQUALS(interaction.IsSane(), true);

        interaction.doorGuid = 0;
        EQUALS(interaction.IsSane(), false);
        interaction.doorGuid  = 1;
        interaction.levelHash = 0;
        EQUALS(interaction.IsSane(), false);
        interaction.levelHash            = 1;
        interaction.presentationSequence = 0;
        EQUALS(interaction.IsSane(), false);
        interaction.presentationSequence = 1;
        interaction.actorIncarnation     = 0;
        EQUALS(interaction.IsSane(), false);
        interaction.actorIncarnation    = 1;
        interaction.actorCombatRevision = 0;
        EQUALS(interaction.IsSane(), false);
        interaction.actorCombatRevision = 1;

        interaction.event = static_cast<KCDC::Shared::RPC::DoorEvent>(255);
        EQUALS(interaction.IsSane(), false);
        // The retired lock edge is refused, never read as a lockpick.
        interaction.event = static_cast<KCDC::Shared::RPC::DoorEvent>(1);
        EQUALS(interaction.IsSane(), false);
        interaction.event       = KCDC::Shared::RPC::DoorEvent::Lockpicked;
        interaction.actorAction = static_cast<KCDC::Shared::RPC::DoorActorAction>(200);
        EQUALS(interaction.IsSane(), false);
        interaction.actorAction = KCDC::Shared::RPC::DoorActorAction::None;

        // A door that was just picked open cannot be reported locked.
        interaction.event  = KCDC::Shared::RPC::DoorEvent::Lockpicked;
        interaction.locked = true;
        EQUALS(interaction.IsSane(), false);
        interaction.locked = false;
        EQUALS(interaction.IsSane(), true);
    });

    IT("round trips the operator's nearest-door question and answer", {
        KCDC::Shared::RPC::DoorNearestQuery query;
        query.requestId = 91;
        EQUALS(query.IsSane(), true);

        MafiaNet::BitStream queryStream;
        query.Serialize(&queryStream, true);
        KCDC::Shared::RPC::DoorNearestQuery decodedQuery;
        decodedQuery.Serialize(&queryStream, false);
        EQUALS(decodedQuery.IsSane(), true);
        EQUALS(decodedQuery.requestId, query.requestId);

        KCDC::Shared::RPC::DoorNearestAnswer answer;
        answer.name      = "gate_trosky";
        answer.requestId = 91;
        answer.doorGuid  = 0x405ec81a9998dd32ULL;
        answer.levelHash = KCDC::Shared::RPC::DoorLevelHash("levels/rataje");
        answer.distance  = 1.75F;
        answer.open      = true;
        answer.locked    = false;
        answer.found     = true;
        EQUALS(answer.IsSane(), true);

        MafiaNet::BitStream answerStream;
        answer.Serialize(&answerStream, true);
        KCDC::Shared::RPC::DoorNearestAnswer decodedAnswer;
        decodedAnswer.Serialize(&answerStream, false);
        EQUALS(decodedAnswer.IsSane(), true);
        STREQUALS(decodedAnswer.name.c_str(), answer.name.c_str());
        EQUALS(decodedAnswer.doorGuid, answer.doorGuid);
        EQUALS(decodedAnswer.open, true);

        // "I looked and there is nothing" is a complete answer.
        KCDC::Shared::RPC::DoorNearestAnswer empty;
        empty.requestId = 91;
        EQUALS(empty.IsSane(), true);
        empty.doorGuid = 5;
        EQUALS(empty.IsSane(), false);
    });

    IT("bounds the animation name before allocation and detects truncation", {
        KCDC::Shared::RPC::DoorInteraction oversized;
        oversized.doorGuid             = 1;
        oversized.levelHash            = 1;
        oversized.presentationSequence = 1;
        oversized.actorIncarnation     = 1;
        oversized.actorCombatRevision  = 1;
        oversized.event                = KCDC::Shared::RPC::DoorEvent::Manipulation;
        oversized.actorAction          = KCDC::Shared::RPC::DoorActorAction::LeftFrontOpen;
        oversized.objectAnimation.assign(KCDC::Shared::RPC::kMaxDoorObjectAnimationLength + 1, 'x');
        EQUALS(oversized.IsSane(), false);

        // A hostile length is refused before a single byte is reserved.
        MafiaNet::BitStream hostileStream;
        std::uint16_t hostileLength = static_cast<std::uint16_t>(KCDC::Shared::RPC::kMaxDoorObjectAnimationLength + 1);
        hostileStream.Serialize(true, hostileLength);
        KCDC::Shared::RPC::DoorInteraction hostileDecoded;
        hostileDecoded.Serialize(&hostileStream, false);
        EQUALS(hostileDecoded.IsSane(), false);
        EQUALS(hostileDecoded.objectAnimation.empty(), true);

        KCDC::Shared::RPC::DoorInteraction interaction;
        interaction.doorGuid             = 1;
        interaction.levelHash            = 1;
        interaction.presentationSequence = 1;
        interaction.actorIncarnation     = 1;
        interaction.actorCombatRevision  = 1;
        MafiaNet::BitStream full;
        interaction.Serialize(&full, true);
        MafiaNet::BitStream truncated(full.GetData(), full.GetNumberOfBytesUsed() - 1, true);
        KCDC::Shared::RPC::DoorInteraction decoded;
        decoded.Serialize(&truncated, false);
        EQUALS(decoded.IsSane(), false);
    });

    IT("rejects control characters in wire text", {
        KCDC::Shared::RPC::DoorInteraction interaction;
        interaction.doorGuid             = 1;
        interaction.levelHash            = 1;
        interaction.presentationSequence = 1;
        interaction.actorIncarnation     = 1;
        interaction.actorCombatRevision  = 1;
        interaction.event                = KCDC::Shared::RPC::DoorEvent::Manipulation;
        interaction.actorAction          = KCDC::Shared::RPC::DoorActorAction::LeftFrontOpen;
        interaction.objectAnimation      = std::string("open\0front", 10);
        EQUALS(interaction.IsSane(), false);

        KCDC::Shared::RPC::DoorNearestAnswer answer;
        answer.requestId = 1;
        answer.doorGuid  = 1;
        answer.levelHash = 1;
        answer.found     = true;
        answer.name      = std::string("gate\0name", 9);
        EQUALS(answer.IsSane(), false);
    });
});

namespace DoorPolicyTest {
    // A ManualKey door at the origin facing +Y: the front is -Y, where the
    // bolt is, and the keyhole is behind it at +Y.
    inline KCDC::Shared::Game::DoorPlacement Placement() {
        return KCDC::Shared::Game::DoorPlacement {"test", 1, "door", 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, "", KCDC::Shared::Game::DoorLockType::ManualKey, true, false, true, true};
    }

    inline KCDC::Shared::Game::DoorReport Report(float actorY) {
        KCDC::Shared::Game::DoorReport report;
        report.actorY = actorY;
        return report;
    }
} // namespace DoorPolicyTest

MODULE(door_policy, {
    IT("carries every level's doors with a normalized facing", {
        EQUALS(KCDC::Shared::Game::g_DoorPlacements.size() > 1000, true);
        const KCDC::Shared::Game::DoorPlacement *door = KCDC::Shared::Game::FindDoorPlacement("klaster", 0x00244decccf5f78aULL);
        EQUALS(door != nullptr, true);
        EQUALS(KCDC::Shared::Game::FindDoorPlacement("kutnohorsko", 0x00244decccf5f78aULL) == nullptr, true);
        for (const KCDC::Shared::Game::DoorPlacement &placement : KCDC::Shared::Game::g_DoorPlacements) {
            EQUALS(std::fabs(std::hypot(placement.forwardX, placement.forwardY) - 1.0F) < 0.001F, true);
        }
    });

    IT("reads the side and the keyhole the way AnimDoor.lua does", {
        KCDC::Shared::Game::DoorPlacement door = DoorPolicyTest::Placement();
        EQUALS(KCDC::Shared::Game::DoorFrontDistance(door, 0.0F, -2.0F) > 0.0F, true);
        EQUALS(KCDC::Shared::Game::DoorFrontDistance(door, 0.0F, 2.0F) < 0.0F, true);
        EQUALS(KCDC::Shared::Game::IsDoorKeySide(door, true), false);
        EQUALS(KCDC::Shared::Game::IsDoorKeySide(door, false), true);
        door.lockReversed = true;
        EQUALS(KCDC::Shared::Game::IsDoorKeySide(door, true), true);
        door.lockType = KCDC::Shared::Game::DoorLockType::None;
        EQUALS(KCDC::Shared::Game::IsDoorKeySide(door, true) || KCDC::Shared::Game::IsDoorKeySide(door, false), false);
    });

    IT("refuses a body out of reach", {
        const KCDC::Shared::Game::DoorPlacement door = DoorPolicyTest::Placement();
        KCDC::Shared::Game::DoorReport report        = DoorPolicyTest::Report(-2.0F);
        report.reportedOpen                          = true;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal == nullptr, true);
        report.actorX = 500.0F;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal != nullptr, true);
    });

    IT("lets a lockpick through only at a locked keyhole it may pick", {
        KCDC::Shared::Game::DoorPlacement door = DoorPolicyTest::Placement();
        KCDC::Shared::Game::DoorReport report  = DoorPolicyTest::Report(2.0F);
        report.event                           = KCDC::Shared::RPC::DoorEvent::Lockpicked;
        report.locked                          = true;

        KCDC::Shared::Game::DoorVerdict verdict = KCDC::Shared::Game::EvaluateDoorReport(door, report);
        EQUALS(verdict.refusal == nullptr, true);
        EQUALS(verdict.actionCount, static_cast<std::size_t>(1));
        EQUALS(verdict.actions[0] == KCDC::Shared::Game::DoorAction::Lockpick, true);
        EQUALS(verdict.locked, false);
        EQUALS(verdict.keySide, true);

        // A server lock does not stop the pick; it stops the key.
        report.lockedByServer = true;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal == nullptr, true);

        report.actorY = -2.0F;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal != nullptr, true);

        // Too near the plane to tell, so the client is believed.
        report.actorY = -0.2F;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal == nullptr, true);

        report.actorY = 2.0F;
        report.locked = false;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal != nullptr, true);

        report.locked     = true;
        door.lockpickable = false;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal != nullptr, true);
    });

    IT("refuses a hand or a key on a door the server locked", {
        const KCDC::Shared::Game::DoorPlacement door = DoorPolicyTest::Placement();
        KCDC::Shared::Game::DoorReport report        = DoorPolicyTest::Report(-2.0F);
        report.locked                                = true;
        report.reportedOpen                          = true;
        report.reportedLocked                        = false;

        KCDC::Shared::Game::DoorVerdict verdict = KCDC::Shared::Game::EvaluateDoorReport(door, report);
        EQUALS(verdict.refusal == nullptr, true);
        EQUALS(verdict.actions[0] == KCDC::Shared::Game::DoorAction::Unlock, true);
        EQUALS(verdict.keySide, false);

        report.lockedByServer = true;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal != nullptr, true);
    });

    IT("refuses opening a locked door unless the same use unlocks it", {
        const KCDC::Shared::Game::DoorPlacement door = DoorPolicyTest::Placement();
        KCDC::Shared::Game::DoorReport report        = DoorPolicyTest::Report(2.0F);
        report.locked                                = true;
        report.reportedOpen                          = true;
        report.reportedLocked                        = true;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal != nullptr, true);

        report.reportedLocked                   = false;
        KCDC::Shared::Game::DoorVerdict verdict = KCDC::Shared::Game::EvaluateDoorReport(door, report);
        EQUALS(verdict.refusal == nullptr, true);
        EQUALS(verdict.actionCount, static_cast<std::size_t>(2));
        EQUALS(verdict.actions[0] == KCDC::Shared::Game::DoorAction::Unlock, true);
        EQUALS(verdict.actions[1] == KCDC::Shared::Game::DoorAction::Open, true);
        EQUALS(verdict.open, true);
        EQUALS(verdict.locked, false);
    });

    IT("asks for nothing when a push leaves the door as the server has it", {
        const KCDC::Shared::Game::DoorPlacement door = DoorPolicyTest::Placement();
        KCDC::Shared::Game::DoorReport report        = DoorPolicyTest::Report(-2.0F);
        report.open                                  = true;
        report.reportedOpen                          = true;

        const KCDC::Shared::Game::DoorVerdict verdict = KCDC::Shared::Game::EvaluateDoorReport(door, report);
        EQUALS(verdict.refusal == nullptr, true);
        EQUALS(verdict.actionCount, static_cast<std::size_t>(0));
        EQUALS(verdict.open, true);
    });

    IT("reads a stale lock bit on a push as lag, not as a lock", {
        const KCDC::Shared::Game::DoorPlacement door = DoorPolicyTest::Placement();
        KCDC::Shared::Game::DoorReport report        = DoorPolicyTest::Report(-2.0F);
        report.open                                  = true;
        report.reportedOpen                          = false;
        report.reportedLocked                        = true;

        const KCDC::Shared::Game::DoorVerdict verdict = KCDC::Shared::Game::EvaluateDoorReport(door, report);
        EQUALS(verdict.refusal == nullptr, true);
        EQUALS(verdict.actionCount, static_cast<std::size_t>(1));
        EQUALS(verdict.actions[0] == KCDC::Shared::Game::DoorAction::Close, true);
        EQUALS(verdict.locked, false);
    });

    IT("shows a player its own world's copy, else the global one, and never another world's", {
        constexpr std::uint32_t global = MafiaNet::VIRTUAL_WORLD_GLOBAL;
        // In world 7: its own copy stands over the global one; world 9's is not its to show.
        EQUALS(KCDC::Shared::Game::WorldCopyRank(7, 7), 2);
        EQUALS(KCDC::Shared::Game::WorldCopyRank(global, 7), 1);
        EQUALS(KCDC::Shared::Game::WorldCopyRank(9, 7), 0);
        // In the global world, which sees every world's copies: only the global one is its own.
        EQUALS(KCDC::Shared::Game::WorldCopyRank(global, global), 2);
        EQUALS(KCDC::Shared::Game::WorldCopyRank(7, global), 0);
        EQUALS(KCDC::Shared::Game::WorldCopyRank(0, global), 0);
        // World 0 is a world like any other, not the global one.
        EQUALS(KCDC::Shared::Game::WorldCopyRank(0, 0), 2);
        EQUALS(KCDC::Shared::Game::WorldCopyRank(global, 0), 1);
    });

    IT("tells a client which world each door and gate copy is", {
        KCDC::Shared::Entities::DoorEntity door;
        door.doorGuid  = 0x00244decccf5f78aULL;
        door.copyWorld = 7;
        KCDC::Shared::Entities::GateEntity gate;
        gate.gateGuid = 0x1234;
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        door.OnSerializeConstruction(writer);
        gate.OnSerializeConstruction(writer);

        // A receiver's copies start as the global one, so the read has to move them.
        KCDC::Shared::Entities::DoorEntity receivedDoor;
        receivedDoor.copyWorld = 3;
        KCDC::Shared::Entities::GateEntity receivedGate;
        receivedGate.copyWorld = 3;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        receivedDoor.OnSerializeConstruction(reader);
        receivedGate.OnSerializeConstruction(reader);
        EQUALS(reader.Good(), true);
        EQUALS(receivedDoor.doorGuid, door.doorGuid);
        EQUALS(receivedDoor.copyWorld, std::uint32_t {7});
        EQUALS(receivedGate.gateGuid, gate.gateGuid);
        EQUALS(receivedGate.copyWorld, MafiaNet::VIRTUAL_WORLD_GLOBAL);
    });

    IT("refuses every use of a door a script made unusable", {
        const KCDC::Shared::Game::DoorPlacement door = DoorPolicyTest::Placement();
        KCDC::Shared::Game::DoorReport report        = DoorPolicyTest::Report(-2.0F);
        report.reportedOpen                          = true;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal == nullptr, true);

        report.usable = false;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal != nullptr, true);

        // A pick is a use too.
        report        = DoorPolicyTest::Report(2.0F);
        report.event  = KCDC::Shared::RPC::DoorEvent::Lockpicked;
        report.locked = true;
        report.usable = false;
        EQUALS(KCDC::Shared::Game::EvaluateDoorReport(door, report).refusal != nullptr, true);
    });

    IT("puts a press to the scripts as retail OnUsed carries it out", {
        // Shut and unlocked: the push alone.
        KCDC::Shared::Game::DoorPress press = KCDC::Shared::Game::EvaluateDoorPress(false, -1.0F, false);
        EQUALS(press.actionCount, static_cast<std::size_t>(1));
        EQUALS(press.actions[0] == KCDC::Shared::Game::DoorAction::Open, true);

        press = KCDC::Shared::Game::EvaluateDoorPress(false, 1.0F, false);
        EQUALS(press.actionCount, static_cast<std::size_t>(1));
        EQUALS(press.actions[0] == KCDC::Shared::Game::DoorAction::Close, true);

        // Locked: the key or the latch first, then the push.
        press = KCDC::Shared::Game::EvaluateDoorPress(false, -1.0F, true);
        EQUALS(press.actionCount, static_cast<std::size_t>(2));
        EQUALS(press.actions[0] == KCDC::Shared::Game::DoorAction::Unlock, true);
        EQUALS(press.actions[1] == KCDC::Shared::Game::DoorAction::Open, true);

        // A pick only starts the minigame, whatever the door's state.
        press = KCDC::Shared::Game::EvaluateDoorPress(true, -1.0F, true);
        EQUALS(press.actionCount, static_cast<std::size_t>(1));
        EQUALS(press.actions[0] == KCDC::Shared::Game::DoorAction::Lockpick, true);

        // OnUsed ignores a door with no direction yet.
        EQUALS(KCDC::Shared::Game::EvaluateDoorPress(false, 0.0F, true).actionCount, static_cast<std::size_t>(0));
    });

    IT("faces a hung door along its rotation's forward axis", {
        const glm::vec2 unturned = KCDC::Shared::Game::HungDoorForward(glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
        EQUALS(std::fabs(unturned.x) < 1e-5F && std::fabs(unturned.y - 1.0F) < 1e-5F, true);

        // A quarter turn about Z takes +Y to -X.
        const glm::vec2 turned = KCDC::Shared::Game::HungDoorForward(glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0F, 0.0F, 1.0F)));
        EQUALS(std::fabs(turned.x + 1.0F) < 1e-5F && std::fabs(turned.y) < 1e-5F, true);

        // Tipped onto its back the axis has no horizontal part; it falls back to +Y.
        const glm::vec2 flat = KCDC::Shared::Game::HungDoorForward(glm::angleAxis(glm::half_pi<float>(), glm::vec3(1.0F, 0.0F, 0.0F)));
        EQUALS(std::fabs(flat.x) < 1e-5F && std::fabs(flat.y - 1.0F) < 1e-5F, true);
    });

    IT("tells a client whether its players may use a door", {
        KCDC::Shared::Entities::DoorEntity door;
        door.doorGuid = 0x00244decccf5f78aULL;
        door.usable   = false;
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        door.OnSerializeConstruction(writer);

        KCDC::Shared::Entities::DoorEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.OnSerializeConstruction(reader);
        EQUALS(reader.Good(), true);
        EQUALS(received.usable, false);
    });
});
