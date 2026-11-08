#include "c_clothing_system.h"

#include "../../patterns.h"

namespace SDK::wh::entitymodule {
    C_ClothingSystem *C_ClothingSystem::GetInstance() {
        return *reinterpret_cast<C_ClothingSystem **>(gPatterns.C_ClothingSystem__Instance);
    }
} // namespace SDK::wh::entitymodule
