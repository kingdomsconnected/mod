#pragma once

#include "core/feature_registry.h"

#include "shared/features/siege/siege_ladder_rpc.h"

#include <atomic>
#include <cstdint>

namespace KCDC::Features::Siege {
    /**
     * Siege ladders on this machine: the tick that builds and tilts every
     * `SiegeLadder`, and the local player's two prompts.
     *
     * A defender on the walk at a standing ladder's top is offered the push,
     * held like the battle prefab's own `@push_ladder` trigger; while that is
     * offered the ladder's climb is not, so the use key is the push's. An
     * attacker beside a fallen ladder's foot is offered the raise. Either
     * press sends `SiegeLadderUse`; the server decides.
     *
     * Game thread only.
     */
    class SiegeLadderModule final: public KCDC::Core::IFeatureService {
      public:
        static SiegeLadderModule &Get();

        void Update() override;
        void Reset() override;

        /** Raised on the loading thread; the tick forgets every native handle. */
        void NativeContextChanged() override;

      private:
        /** Chooses the prompt for the nearest ladder the local player can push or raise from where they stand. */
        void UpdatePrompt(std::uint64_t now);
        void HidePrompt();

        void SendUse(std::uint64_t ladderNetworkId, Shared::RPC::SiegeLadderAction action);

        std::uint32_t _prompt                      = 0;
        std::uint64_t _promptLadder                = 0;
        Shared::RPC::SiegeLadderAction _promptAction = Shared::RPC::SiegeLadderAction::Push;
        std::uint64_t _nextPromptCheck             = 0;

        /** Set from NativeContextChanged, acted on by the tick. */
        std::atomic<bool> _nativeContextLost = false;
    };
} // namespace KCDC::Features::Siege
