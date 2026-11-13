#pragma once

#include "shared/game/horse_gait.h"

#include <cmath>

namespace HorseGaitTests {
    inline bool Near(float a, float b) {
        return std::fabs(a - b) < 1e-4F;
    }
} // namespace HorseGaitTests

MODULE(horse_gait, {
    IT("has the game's four horse move styles, each played by a locomotion blendspace", {
        EQUALS(KCDC::Shared::Game::g_HorseMoveStyles.size(), static_cast<std::size_t>(4));
        EQUALS(KCDC::Shared::Game::g_HorseMoveStyles[0].gait == "walk", true);
        EQUALS(KCDC::Shared::Game::g_HorseMoveStyles[1].gait == "run", true);
        EQUALS(KCDC::Shared::Game::g_HorseMoveStyles[2].gait == "sprint", true);
        EQUALS(KCDC::Shared::Game::g_HorseMoveStyles[3].gait == "dash", true);
        for (const auto &style : KCDC::Shared::Game::g_HorseMoveStyles) {
            bool played = false;
            for (const auto &entry : KCDC::Shared::Game::g_HorseGaitBlendspaces) {
                played = played || entry.gait == style.gait;
            }
            EQUALS(played, true);
        }
    });

    IT("turns a copy toward its owner's facing at a rate, and snaps only a turn no clip could play", {
        // Half a radian wanted, two a second allowed, a tenth of a second: a fifth of a radian.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::StepHorseHeading(0.0F, 0.5F, 2.0F, 0.1F, 1.5708F), 0.2F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::StepHorseHeading(0.0F, -0.5F, 2.0F, 0.1F, 1.5708F), -0.2F), true);
        // Within reach it arrives exactly, and the short way round the circle.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::StepHorseHeading(0.0F, 0.1F, 2.0F, 0.1F, 1.5708F), 0.1F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::StepHorseHeading(3.0F, -3.0F, 2.0F, 0.1F, 1.5708F), 3.2F), true);
        // A frame of no length moves nothing; past the snap the target is taken whole.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::StepHorseHeading(0.0F, 0.5F, 2.0F, 0.0F, 1.5708F), 0.0F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::StepHorseHeading(0.0F, 2.0F, 2.0F, 0.1F, 1.5708F), 2.0F), true);
    });

    IT("carries a copy's turn from one frame to the next while its body has not taken it", {
        // Standing at 125 frames a second, the body 0.3 rad off and not turning: stepped from
        // the body the request is the same 0.9 degrees every frame, which the game does not play.
        float fromBody = 0.0F;
        float carried  = 0.0F;
        for (int frame = 0; frame < 30; ++frame) {
            fromBody = KCDC::Shared::Game::StepHorseHeading(0.0F, 0.3F, 2.0F, 0.008F, 1.5708F);
            carried  = KCDC::Shared::Game::StepHorseHeading(KCDC::Shared::Game::HorseHeadingStart(0.0F, carried, 0.3F, 2.0F, 0.1F, 1.5708F), 0.3F, 2.0F, 0.008F, 1.5708F);
        }
        EQUALS(HorseGaitTests::Near(fromBody, 0.016F), true);
        // Carried, it grows to a tenth of a second of turning ahead of the body, and one step.
        EQUALS(HorseGaitTests::Near(carried, 0.2F + 0.016F), true);

        // A body that took what it was asked is stepped from where it is, as before.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseHeadingStart(0.05F, 0.05F, 0.2F, 0.72F, 0.1F, 1.5708F), 0.05F), true);
        // Never past the target, never away from it, and the short way round the circle.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseHeadingStart(0.0F, 0.05F, 0.01F, 0.72F, 0.1F, 1.5708F), 0.01F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseHeadingStart(0.0F, 0.05F, -0.1F, 0.72F, 0.1F, 1.5708F), 0.0F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseHeadingStart(0.0F, -0.05F, -0.1F, 0.72F, 0.1F, 1.5708F), -0.05F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseHeadingStart(3.1F, -3.1F, -3.0F, 2.0F, 0.1F, 1.5708F), 3.1831853F), true);
        // A turn no clip could play is left to the step, which takes it whole.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseHeadingStart(0.0F, 0.1F, 2.0F, 2.0F, 0.1F, 1.5708F), 0.0F), true);
    });

    IT("picks the style for a speed as the game does: the first whose speed reaches it", {
        EQUALS(KCDC::Shared::Game::HorseMoveStyleForSpeed(0.0F), 0);
        EQUALS(KCDC::Shared::Game::HorseMoveStyleForSpeed(2.3F), 0);
        EQUALS(KCDC::Shared::Game::HorseMoveStyleForSpeed(2.31F), 1);
        // Between the trot's top and the canter's floor the game already canters.
        EQUALS(KCDC::Shared::Game::HorseMoveStyleForSpeed(4.0F), 2);
        EQUALS(KCDC::Shared::Game::HorseMoveStyleForSpeed(8.5F), 3);
        EQUALS(KCDC::Shared::Game::HorseMoveStyleForSpeed(30.0F), 3);
    });

    IT("bands each gait to what every ridden and riderless blendspace of it shows, short of the next style", {
        const auto walk = KCDC::Shared::Game::HorseGaitBandFor(0);
        EQUALS(HorseGaitTests::Near(walk.moveSpeedMin, 1.5F), true);
        EQUALS(HorseGaitTests::Near(walk.moveSpeedMax, 2.2F), true);
        EQUALS(HorseGaitTests::Near(walk.turnSpeedMax, 1.9F), true);

        // The trot's blendspace reaches 3.6, but past 3.49 the game canters.
        const auto trot = KCDC::Shared::Game::HorseGaitBandFor(1);
        EQUALS(HorseGaitTests::Near(trot.moveSpeedMin, 2.4F), true);
        EQUALS(HorseGaitTests::Near(trot.moveSpeedMax, 3.49F), true);
        EQUALS(HorseGaitTests::Near(trot.turnSpeedMax, 1.0F), true);

        // The rider's canter turns half as fast as the riderless one.
        const auto canter = KCDC::Shared::Game::HorseGaitBandFor(2);
        EQUALS(HorseGaitTests::Near(canter.moveSpeedMin, 6.0F), true);
        EQUALS(HorseGaitTests::Near(canter.moveSpeedMax, 8.0F), true);
        EQUALS(HorseGaitTests::Near(canter.turnSpeedMax, 0.72F), true);

        // The last style has no next one to tip into.
        const auto gallop = KCDC::Shared::Game::HorseGaitBandFor(3);
        EQUALS(HorseGaitTests::Near(gallop.moveSpeedMin, 9.7F), true);
        EQUALS(HorseGaitTests::Near(gallop.moveSpeedMax, 14.6F), true);
        EQUALS(HorseGaitTests::Near(gallop.turnSpeedMax, 0.78F), true);

        EQUALS(KCDC::Shared::Game::HorseGaitBandFor(-1).styleId, 0);
        EQUALS(KCDC::Shared::Game::HorseGaitBandFor(9).styleId, 3);
    });

    IT("keeps a moving copy's catch-up inside its owner's gait", {
        const auto walk = KCDC::Shared::Game::HorseGaitBandFor(0);
        // A walking owner and a copy behind: the old +1 m/s asked 2.8, a trot's speed in a walk's stride.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::ClampHorseFollowSpeed(1.8F, false, 2.8F, walk), 2.2F), true);
        // Ahead: slowed no further than the walk's slowest pace.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::ClampHorseFollowSpeed(1.8F, false, 0.8F, walk), 1.5F), true);
        // Inside the band nothing changes.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::ClampHorseFollowSpeed(1.8F, false, 2.0F, walk), 2.0F), true);
        // An owner outside the band -- speeding up through it -- is matched, never slid past.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::ClampHorseFollowSpeed(1.0F, false, 0.6F, walk), 1.0F), true);
        const auto canter = KCDC::Shared::Game::HorseGaitBandFor(2);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::ClampHorseFollowSpeed(4.0F, false, 5.0F, canter), 5.0F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::ClampHorseFollowSpeed(4.0F, false, 3.0F, canter), 4.0F), true);
        // Asked for nothing or less, it stands.
        EQUALS(KCDC::Shared::Game::ClampHorseFollowSpeed(0.6F, false, -0.4F, walk), 0.0F);
    });

    IT("walks a copy of a standing horse at a walk's pace or not at all", {
        const auto walk = KCDC::Shared::Game::HorseGaitBandFor(0);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::ClampHorseFollowSpeed(0.0F, true, 1.0F, walk), 1.5F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::ClampHorseFollowSpeed(0.2F, true, 0.1F, walk), 1.5F), true);
        EQUALS(KCDC::Shared::Game::ClampHorseFollowSpeed(0.0F, true, 0.0F, walk), 0.0F);
    });

    IT("turns the steering bend no faster than the gait's turn range leaves over", {
        const auto gallop = KCDC::Shared::Game::HorseGaitBandFor(3);
        // 0.78 rad/s over a 20 ms frame.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::LimitHorseSteerBend(0.0F, 0.35F, 0.0F, 0.02F, gallop), 0.0156F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::LimitHorseSteerBend(0.0F, -0.35F, 0.0F, 0.02F, gallop), -0.0156F), true);
        // The owner already turning at 0.5 rad/s leaves 0.28.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::LimitHorseSteerBend(0.1F, 0.35F, -0.5F, 0.02F, gallop), 0.1056F), true);
        // Turning at the whole range leaves none, and a frame of no length moves nothing.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::LimitHorseSteerBend(0.1F, 0.35F, 1.0F, 0.02F, gallop), 0.1F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::LimitHorseSteerBend(0.1F, 0.35F, 0.0F, 0.0F, gallop), 0.1F), true);
        // A small step is taken whole.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::LimitHorseSteerBend(0.1F, 0.105F, 0.0F, 0.02F, gallop), 0.105F), true);
    });

    IT("steers a ridden copy onto its owner's facing through its rider's stick", {
        // A tenth of a radian to the left of the body: a quarter of the stick, to the left.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseSteerStick(0.0F, 0.1F, 2.5F, 0.5F, 0.009F, 1.5708F), -0.25F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseSteerStick(0.0F, -0.1F, 2.5F, 0.5F, 0.009F, 1.5708F), 0.25F), true);
        // Held to half the stick, and the short way round the circle.
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseSteerStick(0.0F, 1.0F, 2.5F, 0.5F, 0.009F, 1.5708F), -0.5F), true);
        EQUALS(HorseGaitTests::Near(KCDC::Shared::Game::HorseSteerStick(3.1F, -3.1F, 2.5F, 0.5F, 0.009F, 1.5708F), -0.2079632F), true);
        // Left alone where it already faces its owner's way, and where the turn is taken whole.
        EQUALS(KCDC::Shared::Game::HorseSteerStick(0.0F, 0.005F, 2.5F, 0.5F, 0.009F, 1.5708F), 0.0F);
        EQUALS(KCDC::Shared::Game::HorseSteerStick(0.0F, 2.0F, 2.5F, 0.5F, 0.009F, 1.5708F), 0.0F);
    });
});
