#include "c_dog_companion.h"

#include "../../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::xgenaimodule::activitysystem {
    C_DogCompanion *C_DogCompanion::ResolveForSoul(rpgmodule::I_Soul *soul) {
        if (soul == nullptr) {
            return nullptr;
        }

        // A free function -- `YA` -- whose first integer argument is dead and
        // whose second is what every caller sets: the address of the soul's
        // WUID, which `I_Soul` slot 0 is a two-instruction `lea` for.
        return hook::call<C_DogCompanion *>(gPatterns.ResolveDogActivityOwner, nullptr, soul->GetWuidAddress());
    }
} // namespace SDK::wh::xgenaimodule::activitysystem
