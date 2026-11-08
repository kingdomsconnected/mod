#pragma once

#include <utils/states/state.h>

namespace KCDC::Core::States {
    /** One-shot game-side setup, once the framework is up. */
    class GameSetupState final : public Framework::Utils::States::IState {
      public:
        const char *GetName() const override;
        int32_t GetId() const override;

        bool OnEnter(Framework::Utils::States::Machine *) override;
        bool OnUpdate(Framework::Utils::States::Machine *) override;
        bool OnExit(Framework::Utils::States::Machine *) override;
    };
} // namespace KCDC::Core::States
