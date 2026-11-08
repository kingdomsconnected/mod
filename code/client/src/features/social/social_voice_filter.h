#pragma once

#include <voice/voice_config.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace KCDC::Features::Social {
    /**
     * Per-talker visor effect, owned exclusively by FMOD's mixer thread.
     * Two cascaded one-pole low-pass filters at 1 kHz, with -4.5 dB gain.
     * The dry/wet blend moves over 100 ms, including mid-sentence reversals.
     * Filter history stays warm while bypassed; a different speaker clears
     * it before their first sample. No allocation, locks or game calls.
     */
    class VoiceFilter final {
      public:
        void BeginBlock(std::uint64_t speaker, bool muffled) {
            _target = muffled ? 1.0F : 0.0F;
            if (_speaker != speaker) {
                _speaker = speaker;
                _first   = 0.0F;
                _second  = 0.0F;
                _blend   = _target;
            }
        }

        float Process(float sample) {
            _first += kCoefficient * (sample - _first);
            _second += kCoefficient * (_first - _second);
            // Silence must not leave denormal tails burning mixer CPU.
            if (std::abs(_first) < 1.0e-20F) {
                _first = 0.0F;
            }
            if (std::abs(_second) < 1.0e-20F) {
                _second = 0.0F;
            }
            _blend += std::clamp(_target - _blend, -kBlendStep, kBlendStep);
            return _blend == 0.0F ? sample : sample + _blend * (kGain * _second - sample);
        }

      private:
        static_assert(Framework::Voice::kSampleRate == 48000);
        // 1 - exp(-2*pi*1000/48000), fixed to the voice/mixer sample rate.
        static constexpr float kCoefficient = 0.12269423F;
        static constexpr float kGain        = 0.59566214F;
        static constexpr float kBlendStep   = 1.0F / (0.1F * Framework::Voice::kSampleRate);

        std::uint64_t _speaker = 0;
        float _first           = 0.0F;
        float _second          = 0.0F;
        float _blend           = 0.0F;
        float _target          = 0.0F;
    };
} // namespace KCDC::Features::Social
