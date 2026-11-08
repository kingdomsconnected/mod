#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryPhysics' `PhysicsVars`,
     * embedded at `CPhysicalWorld+0x68`.
     *
     * Its `SolverSettings` base and the members after it follow the
     * published order exactly, except that this build predates the
     * mass-decay settings and `drawHelpersOpacity`: with those left out,
     * every default the world's constructor (0x18099AC38) writes lands on
     * the member CryEngine gives that default to, from nMaxStackSizeMC's 8
     * down to ticksPerSecond's 3e9. Past netDebugDraw the build diverges,
     * so the tail keeps offset names. The offsets in the member comments
     * are CPhysicalWorld's.
     */
    struct PhysicsVars {
        std::int32_t nMaxStackSizeMC; // 0x68, 8 by default
        float maxMassRatioMC; // 0x6C, 50
        std::int32_t nMaxMCiters; // 0x70, 6000
        std::int32_t nMinMCiters; // 0x74, 4
        std::int32_t nMaxMCitersHopeless; // 0x78, 6000
        float accuracyMC; // 0x7C, 0.0005
        float accuracyLCPCG; // 0x80, 0.005
        std::int32_t nMaxContacts; // 0x84, 150
        std::int32_t nMaxPlaneContacts; // 0x88, 8
        std::int32_t nMaxPlaneContactsDistress; // 0x8C, 4
        std::int32_t nMaxLCPCGsubiters; // 0x90, 120
        std::int32_t nMaxLCPCGsubitersFinal; // 0x94, 250
        std::int32_t nMaxLCPCGmicroiters; // 0x98, 12000
        std::int32_t nMaxLCPCGmicroitersFinal; // 0x9C, 25000
        std::int32_t nMaxLCPCGiters; // 0xA0, 5
        float minLCPCGimprovement; // 0xA4, 0.05
        std::int32_t nMaxLCPCGFruitlessIters; // 0xA8, 4
        float accuracyLCPCGnoimprovement; // 0xAC, 0.05
        float minSeparationSpeed; // 0xB0, 0.02
        float maxvCG; // 0xB4, 500
        float maxwCG; // 0xB8, 500
        float maxvUnproj; // 0xBC, 10
        std::int32_t bCGUnprojVel; // 0xC0
        float maxMCMassRatio; // 0xC4, 100
        float maxMCVel; // 0xC8, 15
        std::int32_t maxLCPCGContacts; // 0xCC, 100
        std::int32_t bFlyMode; // 0xD0
        std::int32_t iCollisionMode; // 0xD4
        std::int32_t bSingleStepMode; // 0xD8
        std::int32_t bDoStep; // 0xDC
        float fixedTimestep; // 0xE0
        float timeGranularity; // 0xE4, 0.0001
        float maxWorldStep; // 0xE8, 0.2
        std::int32_t iDrawHelpers; // 0xEC
        std::int32_t iOutOfBounds; // 0xF0, 3
        float maxContactGap; // 0xF4, 0.01
        float maxContactGapPlayer; // 0xF8, 0.01
        float minBounceSpeed; // 0xFC, 1
        std::int32_t bProhibitUnprojection; // 0x100, 1
        std::int32_t bUseDistanceContacts; // 0x104
        float unprojVelScale; // 0x108, 10
        float maxUnprojVel; // 0x10C, 2.5
        float maxUnprojVelRope; // 0x110, 10
        std::int32_t bEnforceContacts; // 0x114, 1
        std::int32_t nMaxSubsteps; // 0x118, 5
        std::int32_t nMaxSurfaces; // 0x11C, 512
        SDK::Vec3 gravity; // 0x120, (0, 0, -9.81)
        std::int32_t nGroupDamping; // 0x12C, 8
        float groupDamping; // 0x130, 0.5
        std::int32_t nMaxSubstepsLargeGroup; // 0x134, 5
        std::int32_t nBodiesLargeGroup; // 0x138, 100
        std::int32_t bBreakOnValidation; // 0x13C
        std::int32_t bLogActiveObjects; // 0x140
        std::int32_t bMultiplayer; // 0x144
        std::int32_t bProfileEntities; // 0x148
        std::int32_t bProfileFunx; // 0x14C
        std::int32_t bProfileGroups; // 0x150
        std::int32_t nGEBMaxCells; // 0x154, 1024
        std::int32_t nMaxEntityCells; // 0x158, 300000
        std::int32_t nMaxAreaCells; // 0x15C, 128
        float maxVel; // 0x160, 100
        float maxVelPlayers; // 0x164, 150
        float maxVelBones; // 0x168, 10
        float maxContactGapSimple; // 0x16C, 0.03
        float penaltyScale; // 0x170, 0.3
        std::int32_t bSkipRedundantColldet; // 0x174, 1
        std::int32_t bLimitSimpleSolverEnergy; // 0x178, 1
        std::int32_t nMaxEntityContacts; // 0x17C, 256
        std::int32_t bLogLatticeTension; // 0x180
        std::int32_t nMaxLatticeIters; // 0x184, 100000
        std::int32_t bLogStructureChanges; // 0x188, 1
        float tickBreakable; // 0x18C, 0.1
        float approxCapsLen; // 0x190, 1.2
        std::int32_t nMaxApproxCaps; // 0x194, 7
        std::int32_t bPlayersCanBreak; // 0x198
        float lastTimeStep; // 0x19C
        std::int32_t bMultithreaded; // 0x1A0
        float breakImpulseScale; // 0x1A4, 1
        float rtimeGranularity; // 0x1A8
        float massLimitDebris; // 0x1AC, 1e10
        std::int32_t flagsColliderDebris; // 0x1B0, 0x80000000
        std::int32_t flagsANDDebris; // 0x1B4, -1
        std::int32_t maxRopeColliderSize; // 0x1B8
        std::int32_t maxSplashesPerObj; // 0x1BC, 3
        float splashDist0; // 0x1C0, 7
        float minSplashForce0; // 0x1C4, 10
        float minSplashVel0; // 0x1C8, 4.5
        float splashDist1; // 0x1CC, 30
        float minSplashForce1; // 0x1D0, 100
        float minSplashVel1; // 0x1D4, 10
        std::int32_t bDebugExplosions; // 0x1D8
        float jointGravityStep; // 0x1DC, 1
        float jointDmgAccum; // 0x1E0, 2
        float jointDmgAccumThresh; // 0x1E4, 0.2
        float timeScalePlayers; // 0x1E8, 1
        float threadLag; // 0x1EC
        std::int32_t numThreads; // 0x1F0, 1
        std::int32_t physCPU; // 0x1F4, 4
        std::int32_t physWorkerCPU; // 0x1F8, 1
        SDK::Vec3 helperOffset; // 0x1FC
        std::int64_t ticksPerSecond; // 0x208, 3e9
        float netInterpTime; // 0x210, 0.1
        float netExtrapMaxTime; // 0x214, 0.5
        std::int32_t netSequenceFrequency; // 0x218, 256
        std::int32_t netDebugDraw; // 0x21C
        std::int32_t state0220;  // 0x220, 1 by default
        std::int32_t state0224;  // 0x224, 20 by default
        float state0228;         // 0x228, 0.02 by default
        std::byte state022C[0x8];
        float state0234;         // 0x234, 0.25 by default
        std::int32_t state0238;  // 0x238
        float state023C;         // 0x23C, 3 by default
        float state0240;         // 0x240, 0.4 by default
        float state0244;         // 0x244, 0.2 by default

      private:
        static void AssertLayout() {
            static_assert(offsetof(PhysicsVars, nMaxStackSizeMC) == 0x00, "PhysicsVars::nMaxStackSizeMC offset mismatch");
            static_assert(offsetof(PhysicsVars, bFlyMode) == 0xD0 - 0x68, "PhysicsVars::bFlyMode offset mismatch");
            static_assert(offsetof(PhysicsVars, gravity) == 0x120 - 0x68, "PhysicsVars::gravity offset mismatch");
            static_assert(offsetof(PhysicsVars, massLimitDebris) == 0x1AC - 0x68, "PhysicsVars::massLimitDebris offset mismatch");
            static_assert(offsetof(PhysicsVars, ticksPerSecond) == 0x208 - 0x68, "PhysicsVars::ticksPerSecond offset mismatch");
        }
    };

    static_assert(sizeof(PhysicsVars) == 0x1E0, "PhysicsVars size mismatch");
} // namespace SDK
