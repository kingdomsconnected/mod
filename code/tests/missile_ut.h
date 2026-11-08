#pragma once

#include "shared/features/player/player_missile_shot_record.h"

namespace MissileTests {
    inline KCDC::Shared::RPC::MissileImpact Hit() {
        KCDC::Shared::RPC::MissileImpact hit;
        hit.launch.shooterNetworkId      = 10;
        hit.launch.sequence              = 7;
        hit.launch.incarnation           = 1;
        hit.launch.npcRevision           = 4;
        hit.launch.weapon                = {0x457033005C23394AULL, 0x479051111CEFB7A8ULL};
        hit.launch.ammunition            = {0x404B8974710E3706ULL, 0xEDF10E67516F3AB2ULL};
        hit.launch.velocity              = {0.0F, 30.0F, 0.0F};
        hit.targetNetworkId              = 20;
        hit.position                     = {0.0F, 12.0F, 0.0F};
        hit.velocity                     = hit.launch.velocity;
        hit.bodyMaterial                 = KCDC::Shared::Game::g_MissileHumanBodyMaterials.front();
        hit.soulAction.incarnation       = 1;
        hit.soulAction.revision          = 4;
        hit.soulAction.targetIncarnation = 1;
        hit.soulAction.complete          = true;
        return hit;
    }
} // namespace MissileTests

MODULE(missile, {
    IT("round-trips a delegated launch identity", {
        auto hit = MissileTests::Hit();
        MafiaNet::BitStream stream;
        hit.Serialize(&stream, true);
        KCDC::Shared::RPC::MissileImpact received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.launch.shooterNetworkId, 10ULL);
        EQUALS(received.launch.npcRevision, 4U);
    });
    IT("rejects a reused shot from a different NPC order or simulator", {
        auto hit = MissileTests::Hit();
        KCDC::Shared::Game::MissileShotRecord shot {hit.launch, 1000, 31000};
        hit.launch.npcRevision++;
        EQUALS(shot.TryClaimImpact(hit, hit.position, 1500), false);
        hit.launch.npcRevision--;
        EQUALS(shot.TryClaimImpact(hit, hit.position, 1500), true);
        EQUALS(shot.TryClaimImpact(hit, hit.position, 1501), false);
    });
    IT("keeps player shots subject to impact distance and lifetime", {
        auto hit               = MissileTests::Hit();
        hit.launch.npcRevision = 0;
        KCDC::Shared::Game::MissileShotRecord shot {hit.launch, 1000, 31000};
        EQUALS(shot.TryClaimImpact(hit, glm::vec3(100.0F), 1500), false);
        EQUALS(shot.TryClaimImpact(hit, hit.position, 31000), false);
        EQUALS(shot.TryClaimImpact(hit, hit.position, 1500), true);
    });
});
