#pragma once

#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/wh/fsw_b_event_instance.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace KCDC::Features::Audio {
    /**
     * An audible range for game sound triggers that were authored without one.
     *
     * A trigger's only distance setting is `distance_culling` in its control
     * file, and the game uses it as a cull and nothing else: an instance
     * farther than that from the listener is not started, and a playing one
     * is stopped. How a sound falls off inside that range is left to the FMOD
     * event, and the cutscene events the game has for set pieces have no
     * falloff at all -- they play at one volume at any distance, and their
     * triggers set the cull to 65535 m.
     *
     * So both halves are supplied here. `Limit` rewrites the cull in the
     * control document as the audio system loads it, and `Attenuate` sets
     * the instance's volume for its distance to the listener as it starts,
     * so it reaches silence at that same range. `Follow` sets it again each
     * time the wrapper re-places a playing instance -- whenever the listener
     * or the sound moves -- so a listener riding away hears it fade, and the
     * cull lands where it is already silent. The range is read back from
     * the event's description, so the control file is the one place it is
     * set. Direction is the FMOD event's own: both siege events are 3D, and
     * the wrapper re-applies their position on the same calls.
     *
     * `Limit` runs on whichever thread loads the controls; `Attenuate` and
     * `Follow` run for every sound the game plays, so they turn an unranged
     * one away on a few lock-free loads before touching a lock or a string.
     */
    class AudioRange final {
      public:
        struct TriggerRange {
            const char *trigger;
            float range;
        };

        static AudioRange &Get();

        /**
         * Sets `distance_culling` on every trigger in an ATL control document
         * that `ranges` names, and fades those triggers' events from then on.
         * The document is replaced by an edited clone; false, and the game's
         * own document kept, when it names none of them.
         */
        bool Limit(SDK::XmlNodeRef &document, std::span<const TriggerRange> ranges);

        /** Sets a starting ranged event's volume for its distance to the listener; anything else is left alone. */
        void Attenuate(const SDK::wh::FSW_B_EVENT_INSTANCE &instance);

        /**
         * Sets a playing ranged event's volume for where the listener is now,
         * after the wrapper re-placed it. `wasWaiting` is its state before:
         * one the wrapper only now starts was fired out of earshot, so it is
         * kept silent for the rest of its life rather than heard late.
         */
        void Follow(const SDK::wh::FSW_B_EVENT_INSTANCE &instance, bool wasWaiting);

        /**
         * Full volume within a twentieth of the range, then the
         * inverse-distance rolloff of a point source (-6 dB per doubling),
         * tapered to silence over the last quarter so it meets the cull.
         */
        static float Gain(double distance, double range);

      private:
        static constexpr std::size_t kMaxRanges = 8;

        /** Whether some ranged event was given this cull, the only distance a ranged description can carry. */
        bool IsRangeInUse(double maxDistance) const;
        bool IsRanged(const char *eventPath) const;

        /** Whether an instance is a ranged event the wrapper holds a live FMOD instance for. */
        bool IsRangedInstance(const SDK::wh::FSW_B_EVENT_INSTANCE &instance) const;

        /** The instance's distance to the listener, both in FMOD's space. */
        static double DistanceToListener(const SDK::wh::FSW_B_EVENT_INSTANCE &instance);

        mutable std::mutex _mutex;
        std::vector<std::string> _eventPaths;

        /**
         * Ranged instances started late, after being fired out of earshot,
         * under `_mutex`. An entry goes when its address next passes
         * `Attenuate`, which every instance does first, so a later instance
         * at a freed one's address is never mistaken for it.
         */
        std::vector<const SDK::wh::FSW_B_EVENT_INSTANCE *> _silenced;

        /** Every distinct range `Limit` has written, in metres; 0 ends the list. Written under `_mutex`, read without it. */
        std::array<std::atomic<double>, kMaxRanges> _ranges {};
    };
} // namespace KCDC::Features::Audio
