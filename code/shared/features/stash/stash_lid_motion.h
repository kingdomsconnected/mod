#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace KCDC::Shared {
    /** Presentation only: normalized lid openness, never permission or stock. */
    class StashLidMotion final {
      public:
        void Reset() {
            *this = StashLidMotion {};
        }

        std::optional<float> Update(bool open, std::uint64_t nowMilliseconds, float clipDurationSeconds) {
            if (!std::isfinite(clipDurationSeconds) || clipDurationSeconds <= 0.0F) {
                return std::nullopt;
            }
            if (!_initialized) {
                // A freshly streamed chest starts at the authoritative pose.
                // Playing a new open action here would invent a past event.
                _openness    = open ? 1.0F : 0.0F;
                _initialized = true;
            }
            else if (nowMilliseconds >= _lastMilliseconds) {
                const double elapsedSeconds = static_cast<double>(nowMilliseconds - _lastMilliseconds) / 1000.0;
                const double distance       = elapsedSeconds / _durationSeconds;
                _openness                   = static_cast<float>(std::clamp(_openness + (_open ? distance : -distance), 0.0, 1.0));
            }
            _open             = open;
            _durationSeconds  = clipDurationSeconds;
            _lastMilliseconds = nowMilliseconds;
            return _openness;
        }

      private:
        bool _initialized               = false;
        bool _open                      = false;
        float _openness                 = 0.0F;
        float _durationSeconds          = 1.0F;
        std::uint64_t _lastMilliseconds = 0;
    };
} // namespace KCDC::Shared
