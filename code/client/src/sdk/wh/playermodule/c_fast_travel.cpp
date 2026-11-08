#include "c_fast_travel.h"

#include "../c_wh_global_environment.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::playermodule {
    C_FastTravel *C_FastTravel::GetInstance() {
        SDK::wh::C_GlobalEnvironment *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        C_PlayerModule *playerModule              = environment != nullptr ? environment->GetPlayerModule() : nullptr;
        return playerModule != nullptr ? playerModule->GetFastTravel() : nullptr;
    }

    E_TravelStartResult C_FastTravel::ValidateTravelStart() const {
        return hook::this_call<E_TravelStartResult>(gPatterns.C_FastTravel__ValidateTravelStart, this);
    }
} // namespace SDK::wh::playermodule
