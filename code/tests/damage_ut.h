#pragma once

#include "shared/features/player/player_damage.h"
#include "shared/features/player/player_respawn.h"
#include "shared/game/body_part_catalog.h"
#include "shared/game/buff_catalog.h"

#include <mafianet/bit_stream.h>

#include <limits>

MODULE(player_damage, {
    IT("resolves a hit subpart to the limb it belongs to", {
        // body_subpart.xml: 1 body_front_up is the torso, 24 head_up the head.
        EQUALS(static_cast<int>(KCDC::Shared::Game::BodyPartOfSubpart(1)), 2);
        EQUALS(static_cast<int>(KCDC::Shared::Game::BodyPartOfSubpart(24)), 1);
        EQUALS(static_cast<int>(KCDC::Shared::Game::BodyPartOfSubpart(-1)), static_cast<int>(KCDC::Shared::Game::kNoBodyPart));
    });

    IT("names only the six human limbs, by the game's own spelling", {
        const KCDC::Shared::Game::BodyPartEntry *leftLeg = KCDC::Shared::Game::FindHumanLimb(5);
        EQUALS(leftLeg != nullptr, true);
        EQUALS(leftLeg->scriptName == "leftLeg", true);
        // horse_legs (10) shares the left leg's injury buff but is no human limb.
        EQUALS(KCDC::Shared::Game::FindHumanLimb(10) == nullptr, true);
        EQUALS(KCDC::Shared::Game::FindHumanLimbByInjuryBuff(KCDC::Shared::Game::FindBuffByName("injured_left_leg")->id) == leftLeg, true);
        EQUALS(KCDC::Shared::Game::FindHumanLimbByInjuryBuff(KCDC::Shared::Game::FindBuffByName("remove_injuries")->id) == nullptr, true);
        // player.heal dereferences both cures by name.
        EQUALS(KCDC::Shared::Game::FindBuffByName("remove_all_posions") != nullptr, true);
    });

    IT("refuses a damage report no body could have taken", {
        KCDC::Shared::RPC::PlayerDamaged report;
        report.amount = 12.5F;
        report.reason = static_cast<std::uint8_t>(KCDC::Shared::Game::DamageReason::Combat);
        EQUALS(report.IsSane(), true);
        report.amount = 0.0F;
        EQUALS(report.IsSane(), false);
        report.amount = std::numeric_limits<float>::infinity();
        EQUALS(report.IsSane(), false);
        report.amount = 1.0F;
        report.reason = KCDC::Shared::Game::kMaxDamageReason + 1;
        EQUALS(report.IsSane(), false);

        KCDC::Shared::RPC::PlayerDied death;
        death.networkId     = 42;
        death.incarnation   = 1;
        death.deathRevision = 1;
        death.reason        = KCDC::Shared::Game::kMaxDamageReason + 1;
        EQUALS(death.IsSane(), false);
    });

    IT("reads a reason this build has no name for as unknown", {
        EQUALS(KCDC::Shared::Game::ToDamageReason(7) == KCDC::Shared::Game::DamageReason::Fall, true);
        EQUALS(KCDC::Shared::Game::ToDamageReason(11) == KCDC::Shared::Game::DamageReason::Unknown, true);
        EQUALS(KCDC::Shared::Game::DamageReasonName(KCDC::Shared::Game::ToDamageReason(9)) == "bleeding", true);
    });

    IT("carries the killer through a death report and back", {
        KCDC::Shared::RPC::PlayerDied sent;
        sent.networkId       = 42;
        sent.incarnation     = 5;
        sent.deathRevision   = 7;
        sent.epoch           = 3;
        sent.killerNetworkId = 99;
        sent.reason          = static_cast<std::uint8_t>(KCDC::Shared::Game::DamageReason::Combat);

        MafiaNet::BitStream bs;
        sent.Serialize(&bs, true);
        KCDC::Shared::RPC::PlayerDied received;
        received.Serialize(&bs, false);

        EQUALS(received.IsSane(), true);
        EQUALS(received.killerNetworkId, sent.killerNetworkId);
        EQUALS(static_cast<int>(received.reason), static_cast<int>(sent.reason));
        EQUALS(received.deathRevision, sent.deathRevision);
    });
});
