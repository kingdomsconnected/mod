#pragma once

#include "shared/game/missile_catalog.h"
#include "shared/features/player/player_missile.h"

namespace KCDC::Shared::Game {
    struct MissileShotRecord {
        RPC::MissileLaunched launch;
        std::uint64_t launchedAt   = 0;
        std::uint64_t expiresAt    = 0;
        bool impactClaimed         = false;
        std::uint64_t impactTarget = 0;
        bool retainedClaimed       = false;

        bool TryClaimImpact(const RPC::MissileImpact &impact, const glm::vec3 &targetPosition, std::uint64_t now, const char **rejection = nullptr) {
            const auto reject = [rejection](const char *reason) {
                if (rejection != nullptr) {
                    *rejection = reason;
                }

                return false;
            };

            if (rejection != nullptr) {
                *rejection = nullptr;
            }

            if (impactClaimed) {
                return reject("already-hit");
            }

            if (!impact.IsSane()) {
                return reject("invalid-impact");
            }

            if (now < launchedAt || now >= expiresAt) {
                return reject("outside-lifetime");
            }

            if (impact.launch.npcRevision != launch.npcRevision || impact.launch.sequence != launch.sequence || impact.launch.incarnation != launch.incarnation || impact.launch.epoch != launch.epoch || impact.launch.weapon != launch.weapon
                || impact.launch.ammunition != launch.ammunition) {
                return reject("launch-mismatch");
            }

            if (!IsHumanMissileBodyMaterial(impact.bodyMaterial)) {
                return reject("body-material");
            }

            if (impact.launch.weaponProperties != launch.weaponProperties || impact.launch.ammunitionProperties != launch.ammunitionProperties) {
                return reject("item-properties");
            }

            if (!IsWorldPosition(targetPosition)) {
                return reject("target-position");
            }

            // Bounds include body extent, replication delay and ballistic travel.
            const auto targetDelta = impact.position - targetPosition;
            const auto flightDelta = impact.position - launch.position;
            const float elapsed    = static_cast<float>(now - launchedAt) * 0.001F;
            const float reach      = glm::length(launch.velocity) * (elapsed + 0.5F) + 4.905F * elapsed * elapsed + 8.0F;

            if (glm::dot(targetDelta, targetDelta) > 25.0F) {
                return reject("target-distance");
            }

            if (glm::dot(flightDelta, flightDelta) > reach * reach) {
                return reject("flight-distance");
            }

            impactClaimed = true;
            impactTarget  = impact.targetNetworkId;

            return true;
        }

        bool TryClaimRetained(const RPC::MissileRetained &retained, std::uint64_t now) {
            if (!impactClaimed || retainedClaimed || !retained.IsSane() || now < launchedAt || now >= expiresAt || retained.sequence != launch.sequence || retained.targetNetworkId != impactTarget
                || !IsValidRetainedMissileProperties(retained.properties, launch.ammunitionProperties)) {
                return false;
            }

            retainedClaimed = true;

            return true;
        }
    };
} // namespace KCDC::Shared::Game
