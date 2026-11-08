#include "initialize.h"
#include "states.h"

#include <utils/states/machine.h>

#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/guimodule/c_gui_module.h"

namespace KCDC::Core::States {
    const char *InitializeState::GetName() const {
        return "Initialize";
    }

    int32_t InitializeState::GetId() const {
        return StateIds::Initialize;
    }

    bool InitializeState::OnEnter(Framework::Utils::States::Machine *) {
        return true;
    }

    bool InitializeState::OnUpdate(Framework::Utils::States::Machine *) {
        // The GUI module, because that is what GameSetup goes on to poll.
        //
        // Not CCryAction: the mod already ticks off CCryAction::PostUpdate by
        // the time this state runs, yet the global that publishes it stays
        // null for several hundred milliseconds more, parking the mod in front
        // of GameSetup while the game reaches its front end. Nothing before
        // SessionConnection reads IGameFramework, and a front end cannot
        // precede the module owning it, so this never outlasts GameSetup's own
        // wait.
        auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        return environment != nullptr && environment->GetGUIModule() != nullptr;
    }

    bool InitializeState::OnExit(Framework::Utils::States::Machine *machine) {
        machine->RequestNextState(StateIds::GameSetup);
        return true;
    }
} // namespace KCDC::Core::States
