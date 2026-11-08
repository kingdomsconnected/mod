#pragma once

#include "../framework/i_action.h"

namespace SDK::wh::playermodule {
    /**
     * RTTI for C_TakeIngredientAction (primary table 0x183A5C308) proves
     * I_AlchemyAction -> I_Action -> _i_multithread_reference_target<int>.
     * The framework's I_ActionImpl remains the separate +0x10 base of
     * C_Action<I_AlchemyAction>, never part of this interface.
     */
    class I_AlchemyAction: public SDK::wh::framework::I_Action {
      public:
        ~I_AlchemyAction() override = default;
    };

    static_assert(sizeof(I_AlchemyAction) == 0x10);
} // namespace SDK::wh::playermodule
