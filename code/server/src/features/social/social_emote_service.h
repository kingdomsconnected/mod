#pragma once

#include "shared/features/social/social_emote_sequence.h"
#include <mafianet/types.h>
#include <map>

namespace KCDC::Features::Social {
    class EmoteService final {
      public:
        void Install();
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid);

      private:
        std::map<MafiaNet::PeerGuid, Shared::Game::EmoteSequence> _senders;
    };
} // namespace KCDC::Features::Social
