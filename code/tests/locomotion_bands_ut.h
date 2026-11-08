#pragma once

#include "shared/game/locomotion_bands.h"

#include <array>
#include <cmath>

namespace LocomotionBandsTests {
    /** Whether every set has a band on the body, each wider than nothing. */
    inline bool Covers(bool female) {
        const std::array<KCDC::Shared::Game::LocomotionGait, 5> gaits {KCDC::Shared::Game::LocomotionGait::Walk, KCDC::Shared::Game::LocomotionGait::Run, KCDC::Shared::Game::LocomotionGait::Sprint, KCDC::Shared::Game::LocomotionGait::StealthWalk, KCDC::Shared::Game::LocomotionGait::StealthRun};
        for (const KCDC::Shared::Game::LocomotionGait gait : gaits) {
            const KCDC::Shared::Game::LocomotionBand *band = KCDC::Shared::Game::FindLocomotionBand(female, gait);
            if (band == nullptr || !(band->maxSpeed > band->minSpeed) || band->minSpeed < 0.0F) {
                return false;
            }
        }
        return true;
    }
} // namespace LocomotionBandsTests

MODULE(locomotion_bands, {
    IT("reads a band for every set either body plays, the crouched female sets from her relaxed ones", {
        EQUALS(LocomotionBandsTests::Covers(false), true);
        EQUALS(LocomotionBandsTests::Covers(true), true);
        EQUALS(KCDC::Shared::Game::FindLocomotionBand(true, KCDC::Shared::Game::LocomotionGait::StealthWalk), KCDC::Shared::Game::FindLocomotionBand(true, KCDC::Shared::Game::LocomotionGait::Walk));
        EQUALS(KCDC::Shared::Game::FindLocomotionBand(true, KCDC::Shared::Game::LocomotionGait::StealthRun), KCDC::Shared::Game::FindLocomotionBand(true, KCDC::Shared::Game::LocomotionGait::Run));
    });

    IT("takes the set from the crouch, then the sprint, then the jog toggle", {
        EQUALS(KCDC::Shared::Game::GaitForInput(false, false, false), KCDC::Shared::Game::LocomotionGait::Walk);
        EQUALS(KCDC::Shared::Game::GaitForInput(true, false, false), KCDC::Shared::Game::LocomotionGait::Run);
        EQUALS(KCDC::Shared::Game::GaitForInput(true, true, false), KCDC::Shared::Game::LocomotionGait::Sprint);
        EQUALS(KCDC::Shared::Game::GaitForInput(false, true, false), KCDC::Shared::Game::LocomotionGait::Sprint);
        EQUALS(KCDC::Shared::Game::GaitForInput(false, true, true), KCDC::Shared::Game::LocomotionGait::StealthWalk);
        EQUALS(KCDC::Shared::Game::GaitForInput(true, false, true), KCDC::Shared::Game::LocomotionGait::StealthRun);
    });

    IT("keeps a walking body's speed inside its band, whichever way the correction points", {
        const KCDC::Shared::Game::LocomotionBand band {false, KCDC::Shared::Game::LocomotionGait::Walk, "walk", 0.6F, 2.0F};
        // Mid-band the nearer edge is 0.7 m/s away.
        const float limit = KCDC::Shared::Game::CorrectionSpeedLimit(&band, 1.3F, 0.3F, 2.5F, 12.0F);
        LESSEREQ(std::fabs(limit - 0.7F), 0.0001F);
        LESSEREQ(1.3F + limit, band.maxSpeed + 0.0001F);
        GREATEREQ(1.3F - limit, band.minSpeed - 0.0001F);
        // Where the old 0.1 s lookahead asked a 0.3 m gap for 3 m/s on top.
        LESSER(limit, 3.0F);
    });

    IT("still closes a gap at the floor speed for a body at its band's edge, standing or outside it", {
        const KCDC::Shared::Game::LocomotionBand band {false, KCDC::Shared::Game::LocomotionGait::Walk, "walk", 0.6F, 2.0F};
        EQUALS(KCDC::Shared::Game::CorrectionSpeedLimit(&band, 1.98F, 0.3F, 2.5F, 12.0F), KCDC::Shared::Game::kMinCorrectionSpeed);
        EQUALS(KCDC::Shared::Game::CorrectionSpeedLimit(&band, 0.0F, 0.3F, 2.5F, 12.0F), KCDC::Shared::Game::kMinCorrectionSpeed);
        EQUALS(KCDC::Shared::Game::CorrectionSpeedLimit(&band, 3.0F, 0.3F, 2.5F, 12.0F), KCDC::Shared::Game::kMinCorrectionSpeed);
        EQUALS(KCDC::Shared::Game::CorrectionSpeedLimit(nullptr, 1.3F, 0.3F, 2.5F, 12.0F), KCDC::Shared::Game::kMinCorrectionSpeed);
    });

    IT("rises toward the full lerp speed as the gap nears the snap, and never past it", {
        const KCDC::Shared::Game::LocomotionBand band {false, KCDC::Shared::Game::LocomotionGait::Walk, "walk", 0.6F, 2.0F};
        const float gentle  = KCDC::Shared::Game::CorrectionSpeedLimit(&band, 1.3F, 1.25F, 2.5F, 12.0F);
        const float halfway = KCDC::Shared::Game::CorrectionSpeedLimit(&band, 1.3F, 1.875F, 2.5F, 12.0F);
        const float atSnap  = KCDC::Shared::Game::CorrectionSpeedLimit(&band, 1.3F, 2.5F, 2.5F, 12.0F);
        LESSEREQ(std::fabs(gentle - 0.7F), 0.0001F);
        LESSEREQ(std::fabs(halfway - (0.7F + (12.0F - 0.7F) * 0.5F)), 0.0001F);
        LESSEREQ(std::fabs(atSnap - 12.0F), 0.0001F);
        LESSEREQ(KCDC::Shared::Game::CorrectionSpeedLimit(&band, 1.3F, 10.0F, 2.5F, 12.0F), 12.0F);
        // A smaller ceiling than the band allows wins.
        EQUALS(KCDC::Shared::Game::CorrectionSpeedLimit(&band, 1.3F, 0.3F, 2.5F, 0.4F), 0.4F);
    });
});
