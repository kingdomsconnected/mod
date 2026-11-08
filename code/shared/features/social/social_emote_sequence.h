#pragma once

#include "shared/features/social/social_emote_rpc.h"

namespace KCDC::Shared::Game {
    class EmoteSequence {
      public:
        bool Accept(const RPC::Emote &event, std::uint64_t now, bool rateLimit) {
            if (!event.IsSane()) {
                return false;
            }
            if (event.phase == RPC::Emote::Start) {
                if (event.sequence <= _lastSequence || (rateLimit && _lastSequence != 0 && now - _lastStart < 750)) {
                    return false;
                }
                _lastSequence = event.sequence;
                _lastStart    = now;
                _emoteId      = event.emoteId;
                _active       = true;
                return true;
            }
            if (!_active || event.sequence != _lastSequence || event.emoteId != _emoteId) {
                return false;
            }
            _active = false;
            return true;
        }

      private:
        std::uint64_t _lastSequence = 0;
        std::uint64_t _lastStart    = 0;
        std::uint16_t _emoteId      = 0;
        bool _active                = false;
    };
} // namespace KCDC::Shared::Game
