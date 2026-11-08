#include "c_buff_manager.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::rpgmodule {
    C_BuffManager *C_BuffManager::GetInstance() {
        return hook::call<C_BuffManager *>(gPatterns.C_BuffManager__GetInstance);
    }
} // namespace SDK::wh::rpgmodule
