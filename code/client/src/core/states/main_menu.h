#pragma once

#include <utils/states/state.h>

#include "core/ui/multiplayer_frontend.h"

namespace KCDC::Core::States {
    /**
     * Not connected: the mod's page is up over the game's own front end, and
     * a connection or offline World Builder takes over the view. A kcdc://
     * launch link connects on entry without putting the page up at all.
     */
    class MainMenuState final: public Framework::Utils::States::IState {
      public:
        const char *GetName() const override;
        int32_t GetId() const override;

        bool OnEnter(Framework::Utils::States::Machine *) override;
        bool OnUpdate(Framework::Utils::States::Machine *) override;
        bool OnExit(Framework::Utils::States::Machine *) override;

      private:
        /** Makes a connection intent the session's target and leaves the menu for it. */
        void ApplyConnectRequest(ConnectRequest request);

        /** Resolves the level and project the menu asked for and hands them to WorldBuilder; false, with the error on the page, when either is unusable. */
        bool StartWorldBuilder(const WorldBuilderRequest &request);

        bool _shouldProceedConnection = false;
    };
} // namespace KCDC::Core::States
