#pragma once

#include "shared/features/door/door_rpc.h"
#include "shared/features/door/gate_entity.h"
#include "shared/features/world/world_level_hash.h"
#include "shared/game/gate_catalog.h"

#include <cmath>
#include <cstddef>
#include <string_view>

MODULE(gate_protocol, {
    IT("times every motion from the clip lengths its DBA stores", {
        const auto *portcullis = KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x4B5D68EB986FDA15ULL);
        const auto *bridge     = KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x495C74B33D651072ULL);
        EQUALS(portcullis != nullptr && bridge != nullptr, true);

        // The same numbers IAnimationSet::GetDuration_sec gave in game.
        const KCDC::Shared::Game::GateAsset &bars = KCDC::Shared::Game::g_GateAssets[portcullis->assetIndex];
        EQUALS(bars.openDurationMs, 17033U);
        EQUALS(bars.closeDurationMs, 6967U);
        EQUALS(bars.openedIdleDurationMs, 2967U);

        // The drawbridge lowers by running its lifting clip backwards.
        const KCDC::Shared::Game::GateAsset &deck = KCDC::Shared::Game::g_GateAssets[bridge->assetIndex];
        EQUALS(deck.closeDurationMs, 7733U);
        EQUALS(deck.openDurationMs, deck.closeDurationMs);
    });

    IT("starts every gate open, as its stand-in shows it, with cycles timed from its asset", {
        KCDC::Shared::Entities::GateEntity gate;
        EQUALS(static_cast<int>(gate.state), static_cast<int>(KCDC::Shared::Entities::GateState::Open));
        EQUALS(gate.CurrentCycleDurationMs(), 0U);

        gate.assetIndex = KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x4B5D68EB986FDA15ULL)->assetIndex;
        gate.state      = KCDC::Shared::Entities::GateState::Closing;
        EQUALS(gate.CurrentCycleDurationMs(), 6967U);
        gate.state = KCDC::Shared::Entities::GateState::Opening;
        EQUALS(gate.CurrentCycleDurationMs(), 17033U);
    });

    IT("reverses a motion from the pose the gate is in, not from the clock", {
        const KCDC::Shared::Game::GateAsset &bars = KCDC::Shared::Game::g_GateAssets[KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x4B5D68EB986FDA15ULL)->assetIndex];

        // The bars are down within the first fifth of the 7s close, then hold.
        EQUALS(KCDC::Shared::Entities::GateMotionOpenness(bars, false, 0.2F) <= 0.02F, true);
        EQUALS(KCDC::Shared::Entities::GateMotionOpenness(bars, false, 0.0F) > 0.9F, true);

        // Reversed 4.3s into the close, as the tester did: the bars are down,
        // so the open clip starts from its beginning rather than 38% up.
        const float shut = KCDC::Shared::Entities::GateMotionOpenness(bars, false, 4333.0F / 6967.0F);
        EQUALS(KCDC::Shared::Entities::GateMotionPhaseAt(bars, true, shut), 0.0F);

        // Reversed halfway up, the close takes over partway through its slam.
        const float half  = KCDC::Shared::Entities::GateMotionOpenness(bars, true, 0.45F);
        const float phase = KCDC::Shared::Entities::GateMotionPhaseAt(bars, false, half);
        EQUALS(phase > 0.0F && phase < 0.2F, true);
        EQUALS(std::fabs(KCDC::Shared::Entities::GateMotionOpenness(bars, false, phase) - half) < 0.02F, true);
    });

    IT("reverses a gate that runs one clip both ways by mirroring its time", {
        const KCDC::Shared::Game::GateAsset &deck = KCDC::Shared::Game::g_GateAssets[KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x495C74B33D651072ULL)->assetIndex];
        const float openness                      = KCDC::Shared::Entities::GateMotionOpenness(deck, false, 0.3F);
        EQUALS(std::fabs(KCDC::Shared::Entities::GateMotionPhaseAt(deck, true, openness) - 0.7F) < 0.001F, true);
    });

    IT("displaces each animated gate's opened stand-in, collider layers included", {
        for (const KCDC::Shared::Game::GatePlacement &placement : KCDC::Shared::Game::g_GatePlacements) {
            if (placement.IsSwapped()) {
                continue;
            }
            EQUALS(placement.displacedBrushes.empty(), false);
            const std::string_view mesh(placement.displacedBrushes[0].mesh);
            EQUALS(mesh.size() > 11 && mesh.substr(mesh.size() - 11) == "_opened.cgf", true);
            EQUALS(placement.displacedBrushes[0].colliders.empty(), false);
            for (const KCDC::Shared::Game::GateBrush &brush : placement.displacedBrushes) {
                for (const KCDC::Shared::Game::GateCollider &collider : brush.colliders) {
                    EQUALS(collider.recordIndex < collider.recordCount, true);
                }
            }
        }
    });

    IT("displaces the NPC-only wall the portcullis layer brings in, and nothing else", {
        const auto *portcullis = KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x4B5D68EB986FDA15ULL);
        const auto *bridge     = KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x495C74B33D651072ULL);
        EQUALS(portcullis->displacedBrushes.size(), static_cast<std::size_t>(2));
        const KCDC::Shared::Game::GateBrush &wall = portcullis->displacedBrushes[1];
        EQUALS(std::string_view(wall.mesh) == "%level%/Brush/designer_b7b3a9c2-32c9-4da8-a4ab-26d8a19eee68.cgf", true);
        EQUALS(wall.colliders.size(), static_cast<std::size_t>(1));
        EQUALS(std::string_view(wall.colliders[0].layer) == portcullis->layer, true);
        EQUALS(bridge->displacedBrushes.size(), static_cast<std::size_t>(1));
    });

    IT("settles a motion into the state it heads for", {
        EQUALS(KCDC::Shared::Entities::IsGateMoving(KCDC::Shared::Entities::GateState::Opening), true);
        EQUALS(KCDC::Shared::Entities::IsGateMoving(KCDC::Shared::Entities::GateState::Closing), true);
        EQUALS(KCDC::Shared::Entities::IsGateMoving(KCDC::Shared::Entities::GateState::Open), false);
        EQUALS(KCDC::Shared::Entities::IsGateMoving(KCDC::Shared::Entities::GateState::Closed), false);

        EQUALS(static_cast<int>(KCDC::Shared::Entities::GateStateAfter(KCDC::Shared::Entities::GateState::Opening)), static_cast<int>(KCDC::Shared::Entities::GateState::Open));
        EQUALS(static_cast<int>(KCDC::Shared::Entities::GateStateAfter(KCDC::Shared::Entities::GateState::Closing)), static_cast<int>(KCDC::Shared::Entities::GateState::Closed));
        EQUALS(static_cast<int>(KCDC::Shared::Entities::GateStateAfter(KCDC::Shared::Entities::GateState::Open)), static_cast<int>(KCDC::Shared::Entities::GateState::Open));
        EQUALS(static_cast<int>(KCDC::Shared::Entities::GateStateAfter(KCDC::Shared::Entities::GateState::Closed)), static_cast<int>(KCDC::Shared::Entities::GateState::Closed));
    });

    IT("swaps the Ratborsch, Nebakov and Ruthard stables and palace gates between layers, open from the start", {
        const auto *ratborsch = KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x486E35908C959192ULL);
        const auto *nebakov   = KCDC::Shared::Game::FindGatePlacement("trosecko", 0x454FA357B47CB32FULL);
        const auto *stables   = KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x4DDDB9E0F7150B3BULL);
        const auto *palace    = KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x4139382FB47ECA21ULL);
        EQUALS(ratborsch != nullptr && nebakov != nullptr && stables != nullptr && palace != nullptr, true);

        for (const auto *placement : {ratborsch, nebakov, stables, palace}) {
            EQUALS(placement->IsSwapped(), true);
            EQUALS(placement->closedLayers.empty(), false);
            EQUALS(placement->displacedBrushes.empty(), true);
            EQUALS(std::string_view(placement->layer).empty(), true);

            // No layer is in two sets, or a state would switch it both ways.
            for (const char *open : placement->openLayers) {
                for (const char *other : placement->closedLayers) {
                    EQUALS(std::string_view(open) == other, false);
                }
                for (const char *other : placement->clearedLayers) {
                    EQUALS(std::string_view(open) == other, false);
                }
            }
            for (const char *closed : placement->closedLayers) {
                for (const char *other : placement->clearedLayers) {
                    EQUALS(std::string_view(closed) == other, false);
                }
            }

            const KCDC::Shared::Game::GateAsset &doors = KCDC::Shared::Game::g_GateAssets[placement->assetIndex];
            EQUALS(static_cast<int>(doors.kind), static_cast<int>(KCDC::Shared::Game::GateKind::Doors));
            EQUALS(std::string_view(KCDC::Shared::Game::GateKindName(doors.kind)) == "gate", true);
        }

        // The quest barrier on the Ratborsch gate line never stands, the
        // Ruthard stables gate opens both leaves, not the one the story shows,
        // and the palace gate's wicket-door version stays out of the world.
        EQUALS(std::string_view(ratborsch->clearedLayers[0]) == "gateBarrier_42dacb18-e0a1-4d4a-af1d-0d0c3a740a8d", true);
        EQUALS(std::string_view(stables->openLayers[0]) == "_enviro_5de784db-3571-4f44-bec1-a0c85c035e65", true);
        EQUALS(std::string_view(stables->clearedLayers[0]) == "_enviro_0463fd7e-21b0-40cf-85a0-3e1769ee7938", true);
        EQUALS(std::string_view(palace->openLayers[0]) == "_enviro_c016c439-3032-4d4e-aced-c21f4e0eb777", true);
        EQUALS(std::string_view(palace->clearedLayers[0]) == "_common_ec1df05e-8a49-4ff7-b119-9f66d7765610", true);
    });

    IT("keeps the Malesov quest door stand-in out of the world", { EQUALS(KCDC::Shared::Game::IsClearedLevelLayer("kutnohorsko", "_common_d959d196-66c3-4e10-94e4-51131e44d942"), true); });

    IT("settles a swapped gate's cycle at once, at either end", {
        KCDC::Shared::Entities::GateEntity gate;
        gate.assetIndex = KCDC::Shared::Game::FindGatePlacement("kutnohorsko", 0x486E35908C959192ULL)->assetIndex;
        EQUALS(static_cast<int>(gate.state), static_cast<int>(KCDC::Shared::Entities::GateState::Open));

        gate.state = KCDC::Shared::Entities::GateState::Closing;
        EQUALS(gate.CurrentCycleDurationMs(), 0U);
        EQUALS(gate.OpennessAfter(0) <= 0.001F, true);
        gate.state = KCDC::Shared::Entities::GateState::Opening;
        EQUALS(gate.CurrentCycleDurationMs(), 0U);
        EQUALS(gate.OpennessAfter(0) >= 0.999F, true);
        EQUALS(KCDC::Shared::Entities::GateMotionPhaseAt(gate.Asset(), true, 0.0F), 0.0F);
    });

    IT("shares one level hash with doors", {
        EQUALS(KCDC::Shared::RPC::LevelHash("kutnohorsko"), KCDC::Shared::RPC::DoorLevelHash("kutnohorsko"));
        EQUALS(KCDC::Shared::RPC::LevelHash("kutnohorsko") == KCDC::Shared::RPC::LevelHash("trosko"), false);
        EQUALS(KCDC::Shared::RPC::LevelHash("") == 0U, false);
    });
});
