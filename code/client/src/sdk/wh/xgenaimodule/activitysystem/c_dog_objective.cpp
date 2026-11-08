#include "sdk/wh/xgenaimodule/activitysystem/c_dog_objective.h"

#include "sdk/patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::xgenaimodule::activitysystem {
    void C_DogObjective::SelectNextWaitBehaviour() {
        hook::this_call<void>(gPatterns.C_WaitDogObjective__SelectNextWaitBehavior, this);
    }

    C_DogObjective *C_DogObjectiveSet::GetObjective(E_DogObjective objective) const {
        return hook::this_call<C_DogObjective *>(gPatterns.C_DogCompanion__GetObjective, this, objective);
    }

    void C_DogObjectivePlanner::DeactivateRunningObjective() {
        hook::this_call<void>(gPatterns.C_DogCompanion__DeactivateRunningObjective, this);
    }
} // namespace SDK::wh::xgenaimodule::activitysystem
