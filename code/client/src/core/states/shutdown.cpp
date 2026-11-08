#include "shutdown.h"
#include "states.h"

namespace KCDC::Core::States {
    const char *ShutdownState::GetName() const {
        return "Shutdown";
    }

    int32_t ShutdownState::GetId() const {
        return StateIds::Shutdown;
    }

    bool ShutdownState::OnEnter(Framework::Utils::States::Machine *) {
        return true;
    }

    bool ShutdownState::OnUpdate(Framework::Utils::States::Machine *) {
        return true;
    }

    bool ShutdownState::OnExit(Framework::Utils::States::Machine *) {
        return true;
    }
} // namespace KCDC::Core::States
