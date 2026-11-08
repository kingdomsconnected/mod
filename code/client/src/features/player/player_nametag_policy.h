#pragma once

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Player {
    /**
     * What the nametag pass remembers about one body between frames.
     *
     * It lives on the body rather than in the pass because the pass keeps no
     * table of bodies: it is rebuilt from replication every frame, and a fade
     * that restarted with it would flicker. Declared here rather than inside
     * `Human` because an NPC carries one too, and an NPC has no business
     * naming a player type to do it.
     */
    struct NametagVisibility {
        /** `ImGui::GetTime()` when the last ray ran; negative for never. */
        double checkedAt = -1.0;
        bool clear       = false;
        float fade       = 0.0F;

        /** The talking icon's own fade, so it eases in and out instead of popping. */
        float voiceFade = 0.0F;
    };

    /**
     * How far the overlay names a body at the hour it is standing in.
     *
     * The signal is the world clock's hour. Weather and interiors are not
     * modelled; both would need a separate input.
     */
    struct NametagLightCurve {
        // Near enough to recognise a face, rather than far enough to scan a
        // field with. Night lands at about a conversation's distance.
        static constexpr float kDaylightDistance = 25.0F;
        static constexpr float kDarknessDistance = 5.0F;

        // The bands daylight ramps across, in hours. These are the tunable
        // part: KCD2 authors its own dawn and dusk per preset, and nothing
        // here reads them back.
        static constexpr float kDawnStart = 5.0F;
        static constexpr float kDawnEnd   = 7.0F;
        static constexpr float kDuskStart = 18.0F;
        static constexpr float kDuskEnd   = 21.0F;

        /** Full spans per second, so a dusk transition does not snap tags in. */
        static constexpr float kAdaptSpeed = 1.0F;

        /** 1 in full day, 0 at night, ramped across dawn and dusk. */
        static float DaylightFractionAt(float hour) {
            if (!std::isfinite(hour)) {
                return 1.0F;
            }

            hour = std::fmod(hour, 24.0F);
            if (hour < 0.0F) {
                hour += 24.0F;
            }

            if (hour <= kDawnStart || hour >= kDuskEnd) {
                return 0.0F;
            }
            if (hour < kDawnEnd) {
                return (hour - kDawnStart) / (kDawnEnd - kDawnStart);
            }
            if (hour <= kDuskStart) {
                return 1.0F;
            }
            return (kDuskEnd - hour) / (kDuskEnd - kDuskStart);
        }

        /** An unreadable hour answers with the daylight reach, never the floor. */
        static float DrawDistanceFor(float hour) {
            return kDarknessDistance + (kDaylightDistance - kDarknessDistance) * DaylightFractionAt(hour);
        }
    };

    /**
     * How the pass rations its occlusion rays.
     *
     * The camera-to-neck ray is the only part of the pass whose cost grows
     * with the session, so it runs on a fixed per-frame budget. Authority
     * expires, which is what makes spending it safe: a clear answer stops
     * drawing a tag at `kMaxAge` whether or not anything refreshed it, so
     * running out of budget can delay a tag appearing but can never keep one
     * alive over a player who has stepped behind cover.
     */
    struct NametagRayPolicy {
        static constexpr int kBudgetPerFrame = 16;
        static constexpr double kInterval    = 0.08;
        static constexpr double kMaxAge      = 0.25;

        /** Occlusion fades per second, so both edges ramp instead of popping. */
        static constexpr float kFadeSpeed = 8.0F;

        /** Talking icon, in full fades per second: quick to appear, slower to leave. */
        static constexpr float kVoiceFadeInSpeed  = 10.0F;
        static constexpr float kVoiceFadeOutSpeed = 4.0F;

        /** Covers the gap between the replicated position the pre-cull runs on and the interpolated body. */
        static constexpr float kPreCullSlack = 4.0F;

        static bool WantsTrace(double checkedAt, double now) {
            return checkedAt < 0.0 || now - checkedAt >= kInterval;
        }

        static bool Authorises(bool clear, double checkedAt, double now) {
            return clear && checkedAt >= 0.0 && now - checkedAt < kMaxAge;
        }
    };
} // namespace KCDC::Features::Player
