#pragma once

#include "human_soul_ut.h"
#include "shared/features/player/player_death.h"

MODULE(player_death, {
    IT("accepts one death event per reported life and rejects duplicate or older reports", {
        KCDC::Shared::RPC::PlayerDied report;
        report.networkId     = 42;
        report.incarnation   = 5;
        report.deathRevision = 7;
        report.epoch         = 3;
        KCDC::Shared::RPC::RespawnPlayer previous;
        EQUALS(KCDC::Shared::Game::AcceptsDeathReport(report, previous), true);
        previous = report;
        EQUALS(KCDC::Shared::Game::AcceptsDeathReport(report, previous), false);
        report.deathRevision++;
        EQUALS(KCDC::Shared::Game::AcceptsDeathReport(report, previous), false);
        report.incarnation = 4;
        EQUALS(KCDC::Shared::Game::AcceptsDeathReport(report, previous), false);
        report.incarnation = 6;
        EQUALS(KCDC::Shared::Game::AcceptsDeathReport(report, previous), true);
        previous.incarnation = 0xFFFFFFFFU;
        report.incarnation   = 1;
        EQUALS(KCDC::Shared::Game::AcceptsDeathReport(report, previous), true);
        report.valid = false;
        EQUALS(KCDC::Shared::Game::AcceptsDeathReport(report, previous), false);
    });

    IT("accepts a server permission for the observed dead body", {
        auto soul                 = HumanSoulTests::Sample();
        soul.resources[0].current = 0.0F;
        soul.revision             = 12;
        KCDC::Shared::RPC::RespawnPlayer permission {42, soul.incarnation, 10, 3};
        EQUALS(KCDC::Shared::Game::AcceptsRespawn(permission, 42, 3, soul), true);
        soul.resources[0].current = 1.0F;
        EQUALS(KCDC::Shared::Game::AcceptsRespawn(permission, 42, 3, soul), false);
    });

    IT("rejects another owner epoch body and future death", {
        auto soul                 = HumanSoulTests::Sample();
        soul.resources[0].current = 0.0F;
        soul.revision             = 12;
        KCDC::Shared::RPC::RespawnPlayer permission {42, soul.incarnation, 10, 3};
        EQUALS(KCDC::Shared::Game::AcceptsRespawn(permission, 43, 3, soul), false);
        EQUALS(KCDC::Shared::Game::AcceptsRespawn(permission, 42, 4, soul), false);
        permission.incarnation++;
        EQUALS(KCDC::Shared::Game::AcceptsRespawn(permission, 42, 3, soul), false);
        permission.incarnation--;
        permission.deathRevision = 13;
        EQUALS(KCDC::Shared::Game::AcceptsRespawn(permission, 42, 3, soul), false);
    });

    IT("cannot resurrect a second death using a delayed first permission", {
        auto soul                 = HumanSoulTests::Sample();
        soul.resources[0].current = 0.0F;
        soul.revision             = 12;
        KCDC::Shared::RPC::RespawnPlayer permission {42, soul.incarnation, 10, 3};
        soul.incarnation = KCDC::Shared::Game::HumanSoulState::Advance(soul.incarnation);
        soul.revision++;
        EQUALS(KCDC::Shared::Game::AcceptsRespawn(permission, 42, 3, soul), false);
    });

    IT("accepts revision wrap but rejects malformed and truncated permissions", {
        auto soul                 = HumanSoulTests::Sample();
        soul.resources[0].current = 0.0F;
        soul.revision             = 1;
        KCDC::Shared::RPC::RespawnPlayer permission {42, soul.incarnation, 0xFFFFFFFFU, 3};
        EQUALS(KCDC::Shared::Game::AcceptsRespawn(permission, 42, 3, soul), true);
        MafiaNet::BitStream stream;
        permission.Serialize(&stream, true);
        KCDC::Shared::RPC::RespawnPlayer decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.networkId, permission.networkId);
        EQUALS(decoded.deathRevision, permission.deathRevision);
        MafiaNet::BitStream empty;
        decoded.Serialize(&empty, false);
        EQUALS(decoded.IsSane(), false);
        permission.incarnation = 0;
        EQUALS(permission.IsSane(), false);
    });
});
