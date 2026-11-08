#pragma once

#include "sdk/fmod/fmod_api.h"
#include "sdk/fmod/fmod_types.h"
#include "social_voice_filter.h"

#include <voice/client/playout_buffer.h>
#include <voice/voice_config.h>

#include <glm/glm.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace KCDC::Features::Social {
    /**
     * One remote talker, playing through the game's own FMOD.
     *
     * A 3D channel held open for the life of the slot and handed from talker
     * to talker. It plays a looping silent sample, and a DSP at the tail of its
     * chain -- ahead of the fader that applies volume and 3D panning -- replaces
     * that silence with the talker's audio, pulled straight from the playout
     * buffer on FMOD's mixer thread.
     *
     * Not a user-created stream: those are filled by FMOD's stream thread into
     * a double buffer the mixer reads behind it, and whenever that thread is
     * late the mixer replays the half it already played. Players heard it as
     * noise at the end of every word -- 10-20ms of speech repeated from 40ms
     * earlier, several times a second. A DSP has no buffer of its own between
     * the playout buffer and the mix, so there is nothing stale to replay.
     *
     * The channel never stops: one created per talker would put an allocation
     * and a `playSound` on the path of everyone who starts speaking, and would
     * let FMOD's voice stealing decide who is audible instead of the
     * framework's distance-based admission.
     *
     * Two threads meet here and nowhere else. `Submit`, `Place` and the
     * binding calls are the game thread; `Render` is FMOD's mixer thread. They
     * share the PCM ring and atomic speaker/visor state, so `Render` never
     * has to call back into FMOD.
     */
    class Voice final {
      public:
        Voice() = default;
        ~Voice();

        // The mixer thread finds this object by the address of its sound, so
        // neither it nor its ring may be copied.
        Voice(const Voice &)            = delete;
        Voice &operator=(const Voice &) = delete;

        /**
         * Creates the source, the DSP and the channel, and starts the channel
         * silent and unplaced.
         *
         * `group` carries the player's volume and the game's own ducking.
         * `read` and `shouldProcess` are the module's mixer-thread entry
         * points: they live there because resolving an FMOD_DSP back to its
         * slot is a question about every slot at once.
         */
        bool Open(SDK::Fmod::FMOD_SYSTEM *system, SDK::Fmod::FMOD_CHANNELGROUP *group, SDK::Fmod::FMOD_DSP_READ_CALLBACK read, SDK::Fmod::FMOD_DSP_SHOULDIPROCESS_CALLBACK shouldProcess);

        /** Idempotent. Takes the DSP off the channel before releasing anything. */
        void Close();

        bool IsOpen() const {
            return _dsp.load(std::memory_order_relaxed) != nullptr;
        }

        /** 0 when free. Published last on bind and cleared first on release. */
        std::uint64_t GetSpeaker() const {
            return _speaker.load(std::memory_order_relaxed);
        }

        /**
         * Free, and the mixer has consumed what the last talker left. Bound
         * again any earlier, a newcomer's first words would come out behind
         * the tail of somebody else's sentence.
         */
        bool IsAvailable() const {
            return _speaker.load(std::memory_order_relaxed) == 0 && _audio.Available() == 0;
        }

        void Bind(std::uint64_t speaker);
        void Release();

        /** Game thread. Drops the frame if the ring is full rather than stalling. */
        void Submit(const std::int16_t *mono, std::uint32_t samples);

        /**
         * Places the voice and unmutes it. `position` is CryEngine world
         * space; `range` is the radius the server relays within, so the curve
         * reaches zero exactly where the frames stop.
         * `muffled` publishes the closed-visor target to the mixer.
         */
        void Place(const glm::vec3 &position, float range, float fullVolumeFraction, bool muffled);

        /**
         * Silences a bound voice whose position is unknown this tick. Muted
         * rather than stopped, so the audio still drains and a talker who
         * reappears is not preceded by everything that piled up meanwhile.
         */
        void Mute();

        /**
         * Moves the playing channel to another group without stopping it,
         * so a talker mid-sentence carries on through the switch.
         */
        void SetGroup(SDK::Fmod::FMOD_CHANNELGROUP *group);

        /**
         * FMOD's mixer thread, reached only from `VoiceService::DspRead`.
         * Always writes `length` samples to each of `channels`, silence where
         * it must, so the channel never runs dry and never has to be
         * restarted.
         */
        void Render(float *out, std::uint32_t length, int channels);

        /**
         * How the mixer thread finds its slot. Atomic because those are the
         * two threads that touch it: published only once the channel is fully
         * built, and retracted before the DSP leaves it, so a callback either
         * finds a ready slot or finds none.
         */
        SDK::Fmod::FMOD_DSP *GetDsp() const {
            return _dsp.load(std::memory_order_acquire);
        }

      private:
        // ~680ms of mono, matching the framework's own per-speaker ring. The
        // playout buffer asserts it holds the deepest latency ceiling.
        static constexpr std::size_t kRingSamples = 32768;

        std::atomic<SDK::Fmod::FMOD_DSP *> _dsp {nullptr};
        SDK::Fmod::FMOD_SOUND *_sound     = nullptr;
        SDK::Fmod::FMOD_CHANNEL *_channel = nullptr;

        // FMOD reads the description for as long as the DSP made from it
        // lives, so it is held here rather than built on the stack.
        SDK::Fmod::FMOD_DSP_DESCRIPTION _description;

        // 0 = free. The mixer reads it to know whether to play what is
        // buffered or throw it away.
        std::atomic<std::uint64_t> _speaker {0};

        // Game-thread placement publishes intent; only Render owns history.
        std::atomic<bool> _muffled {false};
        VoiceFilter _filter;

        // The framework's playout buffer: the same jitter handling, drift
        // trim and per-talker adaptive depth the built-in mixer uses.
        Framework::Voice::PlayoutBuffer<kRingSamples> _audio;

        // Whether the channel is audible, so an unchanged placement does not
        // restate a volume FMOD already has.
        bool _audible = false;
    };
} // namespace KCDC::Features::Social
