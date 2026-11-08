#pragma once

#include "sdk/fmod/fmod_api.h"
#include "sdk/fmod/fmod_types.h"

#include <voice/client/i_voice_source.h>
#include <voice/client/spsc_ring.h>
#include <voice/voice_config.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Social {
    /**
     * The microphone, taken from the game's FMOD rather than a device of the
     * mod's own.
     *
     * Its own unit because it is a different subject from playback with its
     * own vocabulary: FMOD hands back whatever the recording endpoint natively
     * runs at and Opus wants 48kHz mono, so most of what is here is the
     * conversion between the two.
     *
     * Two audio devices in one process is the thing that breaks. Miniaudio's
     * WASAPI backend puts the calling thread into the multi-threaded apartment
     * and holds it there, an exclusive-mode endpoint refuses the second client
     * outright, and the player ends up with one microphone selector in the
     * game and another in Windows. Recording through the system the game
     * already owns leaves exactly one audio backend.
     *
     * Game thread only: FMOD fills the record buffer on its own thread, but
     * nothing here runs there -- the buffer is read by polling a cursor.
     */
    class VoiceMicrophone final: public Framework::Voice::IVoiceSource {
      public:
        VoiceMicrophone() = default;
        ~VoiceMicrophone() override;

        VoiceMicrophone(const VoiceMicrophone &)            = delete;
        VoiceMicrophone &operator=(const VoiceMicrophone &) = delete;

        bool Start() override;
        void Stop() override;

        bool IsRunning() const override {
            return _sound != nullptr;
        }

        bool ReadFrame(std::int16_t *out) override;

        /**
         * The recording drivers FMOD sees, by name, minus the loopback
         * captures WASAPI lists for every output device -- those record the
         * game's own sound, not the player.
         */
        std::vector<std::string> ListDevices() const override;

        void SelectDevice(const std::string &name) override {
            _deviceName = name;
        }

        std::string GetSelectedDevice() const override {
            return _deviceName;
        }

        /**
         * How many times captured audio had to be skipped.
         *
         * Non-zero means the game thread stalled long enough that more was
         * recorded than the conversion ring can carry: the newest audio is
         * kept and the rest dropped, which is a lost word rather than a
         * resynchronisation nobody sees.
         */
        std::uint32_t GetOverrunCount() const {
            return _overruns;
        }

      private:
        // ~340ms of converted mono, matching the framework's own capture ring.
        static constexpr std::size_t kRingSamples = 16384;

        /**
         * One second-order section of the anti-alias filter, used only when
         * the endpoint runs above 48kHz -- where plain interpolation would
         * fold everything above the target's Nyquist back into the speech band
         * as a metallic ring.
         */
        struct Biquad final {
            void Configure(double sampleRate, double cutoffHz, double q);

            void Reset() {
                _z1 = 0.0f;
                _z2 = 0.0f;
            }

            float Process(float x) {
                const float y = _b0 * x + _z1;
                _z1           = _b1 * x - _a1 * y + _z2;
                _z2           = _b2 * x - _a2 * y;
                return y;
            }

          private:
            float _b0 = 1.0f;
            float _b1 = 0.0f;
            float _b2 = 0.0f;
            float _a1 = 0.0f;
            float _a2 = 0.0f;
            float _z1 = 0.0f;
            float _z2 = 0.0f;
        };

        /** Reads whatever the record buffer has gained since the last call. */
        void Pump();

        /**
         * The FMOD driver index for `_deviceName`, or the default (0) when no
         * device is chosen or the chosen one is gone. Driver indices move
         * when devices come and go, so the name is what is kept.
         */
        int ResolveDriver() const;

        /** Downmixes, filters and resamples one locked block into the ring. */
        void Consume(const std::int16_t *interleaved, std::uint32_t frames);

        SDK::Fmod::FMOD_SYSTEM *_system = nullptr;
        SDK::Fmod::FMOD_SOUND *_sound   = nullptr;

        // The chosen device, and the driver it resolved to at Start.
        std::string _deviceName;
        int _driver = 0;

        std::int32_t _rate     = 0;
        std::int32_t _channels = 0;

        // Frames the record buffer holds, and where we have read up to.
        std::uint32_t _recordFrames = 0;
        std::uint32_t _readCursor   = 0;
        std::uint32_t _overruns     = 0;

        // Input frames per output sample. The endpoint already running at the
        // voice format is the common case and is short-circuited entirely.
        double _ratio     = 1.0;
        bool _passthrough = true;

        // Position between _history[1] and _history[2], in input samples.
        double _phase = 0.0;

        // The four points the cubic interpolator needs, newest last.
        std::array<float, 4> _history {};

        // Two sections, giving a fourth-order Butterworth.
        std::array<Biquad, 2> _antiAlias {};

        Framework::Voice::SpscRing<std::int16_t, kRingSamples> _out;
    };
} // namespace KCDC::Features::Social
