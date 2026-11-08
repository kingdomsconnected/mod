#pragma once

#include "join_deadlines.h"
#include "session_startup.h"

#include <cstdint>

namespace KCDC::Core::States {
    /**
     * Dialing the server, and loading the level it named.
     *
     * Admission starts the native level load, held at its completion by the
     * session's readiness task. The registered startup states then own
     * resource startup, replication and placement; SessionConnected lets the
     * game complete the load.
     */
    class SessionConnectionState final: public SessionStartupState {
      public:
        const char *GetName() const override;
        int32_t GetId() const override;

        bool OnEnter(Framework::Utils::States::Machine *) override;
        bool OnUpdate(Framework::Utils::States::Machine *) override;
        bool OnExit(Framework::Utils::States::Machine *) override;
        void OnLevelAndAssetsReady() override;

      private:
        bool _levelRequested = false;
        // One-shot: re-testing every tick would send a second disconnect.
        bool _dlcChecked = false;
        bool _assetsBegun = false;
        bool _assetsReady = false;
        std::uint64_t _downloadShownAtMs = 0;
        std::uint64_t _downloadBeganAtMs = 0;

        std::uint64_t _levelRequestNotBeforeMs = 0;
        // When the server admitted this connection; its resource list follows
        // within milliseconds, so a long silence is a server not answering.
        std::uint64_t _admittedAtMs = 0;
    };
} // namespace KCDC::Core::States
