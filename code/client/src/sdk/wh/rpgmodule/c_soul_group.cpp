#include "c_soul_group.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::rpgmodule {
    bool C_SoulGroup::PossessAnimal(I_Soul *animal, bool insertFront) {
        return hook::this_call<bool>(gPatterns.C_SoulGroup__PossessAnimal, this, animal, insertFront);
    }

    bool C_SoulGroup::ReleaseAnimal(I_Soul *animal) {
        return hook::this_call<bool>(gPatterns.C_SoulGroup__ReleaseAnimal, this, animal);
    }
} // namespace SDK::wh::rpgmodule
