#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::UI {
    /** Carries the identity shown at input time across to the game-thread tick. */
    class HintInputQueue final {
      public:
        void SetTarget(const std::string &action, std::uint32_t hint, std::uint8_t type) {
            const std::lock_guard<std::mutex> lock(_lock);
            if (hint == 0) {
                _targets.erase(action);
            }
            else {
                _targets.insert_or_assign(action, std::pair {hint, type});
            }
        }

        void Capture(const char *action, std::uint8_t type) {
            const std::lock_guard<std::mutex> lock(_lock);
            const auto target = _targets.find(action);
            if (target != _targets.end() && target->second.second == type) {
                _pending.push_back(target->second.first);
            }
        }

        std::vector<std::uint32_t> Drain() {
            const std::lock_guard<std::mutex> lock(_lock);
            std::vector<std::uint32_t> result;
            result.swap(_pending);
            return result;
        }

        void Reset() {
            const std::lock_guard<std::mutex> lock(_lock);
            _targets.clear();
            _pending.clear();
        }

      private:
        std::mutex _lock;
        std::map<std::string, std::pair<std::uint32_t, std::uint8_t>, std::less<>> _targets;
        std::vector<std::uint32_t> _pending;
    };
} // namespace KCDC::Features::UI
