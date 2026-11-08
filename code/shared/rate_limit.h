#pragma once

#include <algorithm>
#include <cstdint>

namespace KCDC::Shared {
    /**
     * A token bucket: `burst` messages at once, then `rate` per second.
     *
     * The inventory service keeps two per connection: a tight one past which
     * a message is dropped, and a looser
     * one past which the sender is disconnected.
     */
    class RateLimit final {
      public:
        RateLimit(double rate, double burst): _rate(rate), _burst(burst), _tokens(burst) {}

        /** Spends one token at `nowMs`; false when the bucket is empty. */
        bool Take(std::uint64_t nowMs) {
            if (_started && nowMs > _last) {
                _tokens = std::min(_burst, _tokens + (static_cast<double>(nowMs - _last) * _rate / 1000.0));
            }
            _last    = nowMs;
            _started = true;
            if (_tokens < 1.0) {
                return false;
            }
            _tokens -= 1.0;
            return true;
        }

      private:
        double _rate;
        double _burst;
        double _tokens;
        std::uint64_t _last = 0;
        bool _started       = false; // A clock may legitimately start at zero.
    };
} // namespace KCDC::Shared
