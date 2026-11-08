#include "game_setup.h"
#include "states.h"

#include <utils/states/machine.h>

#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/guimodule/c_gui_module.h"
#include "sdk/wh/guimodule/c_ui_menu.h"

namespace KCDC::Core::States {
    const char *GameSetupState::GetName() const {
        return "GameSetup";
    }

    int32_t GameSetupState::GetId() const {
        return StateIds::GameSetup;
    }

    bool GameSetupState::OnEnter(Framework::Utils::States::Machine *) {
        return true;
    }

    bool GameSetupState::OnUpdate(Framework::Utils::States::Machine *) {
        // Nothing is loaded before a session, so the only thing to wait on is
        // the game's own front end -- the last thing a boot puts up, and what
        // the mod's page is drawn over.
        auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        auto *guiModule   = environment != nullptr ? environment->GetGUIModule() : nullptr;
        auto *menu        = guiModule != nullptr ? guiModule->GetMenu() : nullptr;
        return menu != nullptr && menu->IsFrontEndContext();
    }

    bool GameSetupState::OnExit(Framework::Utils::States::Machine *machine) {
        machine->RequestNextState(StateIds::MainMenu);
        return true;
    }
} // namespace KCDC::Core::States
