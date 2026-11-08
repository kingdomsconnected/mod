#pragma once

#include <utils/states/state.h>

namespace KCDC::Core::States {
    /** Waits for the game framework to come up. */
    class InitializeState final : public Framework::Utils::States::IState {
      public:
        const char *GetName() const override;
        int32_t GetId() const override;

        bool OnEnter(Framework::Utils::States::Machine *) override;
        bool OnUpdate(Framework::Utils::States::Machine *) override;
        bool OnExit(Framework::Utils::States::Machine *) override;
    };
} // namespace KCDC::Core::States
