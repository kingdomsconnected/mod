#pragma once

#include "player_rock.h"
#include "shared/rate_limit.h"

namespace KCDC::Shared::Game {
    // Server-side ordering, rate and origin limits for one connected body.
    class RockThrowHistory final {
      public:
        bool Accept(const RPC::RockLaunched &message, const glm::vec3 &throwerPosition, std::uint64_t now) {
            const auto offset = message.position - throwerPosition;
            if (!message.IsSane() || !IsFinite(throwerPosition) || message.sequence <= _lastSequence || glm::dot(offset, offset) > 36.0F) {
                return false;
            }
            // A refused throw still spends its sequence, so it cannot be
            // replayed once the bucket refills.
            _lastSequence = message.sequence;
            return _launches.Take(now);
        }

      private:
        std::uint64_t _lastSequence = 0;
        // A burst of four, then two a second.
        Shared::RateLimit _launches {2.0, 4.0};
    };
} // namespace KCDC::Shared::Game
