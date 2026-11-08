#pragma once

#include "shared/features/player/player_soul_state.h"
#include "shared/features/player/player_respawn.h"

#include <cstdint>

namespace KCDC::Shared::Game {
    inline bool AcceptsDeathReport(const RPC::PlayerDied &report, const RPC::RespawnPlayer &previous) {
        return report.IsSane() && (!previous.IsSane() || report.networkId != previous.networkId || report.epoch != previous.epoch || HumanSoulState::IsNewer(report.incarnation, previous.incarnation));
    }

    /** A permission cannot cross bodies or apply again after a revival. */
    inline bool AcceptsRespawn(const RPC::RespawnPlayer &permission, std::uint64_t networkId, std::uint8_t epoch, const HumanSoulState &soul) {
        return permission.IsSane() && permission.networkId == networkId && permission.epoch == epoch && soul.valid && permission.incarnation == soul.incarnation && soul.resources[HumanSoulState::kHealth].current <= 0.0F
               && (permission.deathRevision == soul.revision || HumanSoulState::IsNewer(soul.revision, permission.deathRevision));
    }
} // namespace KCDC::Shared::Game
