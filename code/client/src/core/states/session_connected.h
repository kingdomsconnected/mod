#pragma once

#include <utils/states/state.h>

namespace KCDC::Core::States {
    /** In a session: remote bodies are driven from here. F9 disconnects. */
    class SessionConnectedState final : public Framework::Utils::States::IState {
      public:
        const char *GetName() const override;
        int32_t GetId() const override;

        bool OnEnter(Framework::Utils::States::Machine *) override;
        bool OnUpdate(Framework::Utils::States::Machine *) override;
        bool OnExit(Framework::Utils::States::Machine *) override;
    };
} // namespace KCDC::Core::States
