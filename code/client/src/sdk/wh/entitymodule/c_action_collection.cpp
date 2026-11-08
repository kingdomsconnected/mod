#include "c_action_collection.h"

namespace SDK::wh::entitymodule {
    wh::framework::I_Action *C_ActionCollection::FindActionOfType(std::uint8_t type) const {
        for (wh::framework::I_Action **entry = m_begin; entry != m_end; ++entry) {
            wh::framework::I_Action *action = *entry;
            if (action != nullptr && action->GetActionType() == type) {
                return action;
            }
        }
        return nullptr;
    }
} // namespace SDK::wh::entitymodule
