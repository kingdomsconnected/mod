#pragma once

#include "player_station.h"
#include "shared/features/animation/animation_playback.h"

namespace KCDC::Shared::Game {
    inline AnimationPlayback StationAnimation(const StationState &station) {
        AnimationPlayback result;
        result.revision = station.revision;
        result.active   = station.Active();
        result.loop     = true;
        if (!result.active) {
            return result;
        }
        switch (station.activity) {
        case StationActivity::Dice:
            result.fragment = station.action != 0 ? "DiceShake" : "DiceIdle";
            result.loop     = station.action == 0;
            break;
        case StationActivity::Alchemy: result.fragment = "ApothecaryAlchemyBookIdle"; break;
        case StationActivity::Smithing:
            // A male body's steps are its minigame's own fragments, replayed
            // from `HumanEntity::minigameActions`; this idle is for a body
            // shown none of them yet, and for a female one, whose controller
            // has no Blacksmithing* fragments.
            result.fragment = "MotionIdle";
            break;
        case StationActivity::Sharpening:
            // Likewise: the walk-in, the wheel loop and the walk-out are the
            // owner's own SharpeningMinigame* actions, replayed; this idle is
            // only for a body shown none of them yet.
            result.fragment = "MotionIdle";
            break;
        case StationActivity::None: break;
        }
        return result;
    }
} // namespace KCDC::Shared::Game
