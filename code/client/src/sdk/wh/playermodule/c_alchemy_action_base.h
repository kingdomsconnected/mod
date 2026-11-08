#pragma once

#include "../framework/c_action.h"
#include "c_alchemy.h"
#include "i_alchemy_action.h"

#include <cstddef>

namespace SDK::wh::playermodule {
    /** Empty native parameter type, explicitly named by the action RTTI. */
    struct S_EmptyParams {};
    static_assert(sizeof(S_EmptyParams) == 1);

    /**
     * RTTI 0x183A67AB8 and constructor 0x1809CC290 prove this exact template
     * inheritance. The constructor receives C_Alchemy in RDX and stores
     * it at +0x58. The environment-owned module at +0x60 remains opaque.
     */
    template <typename TParams, typename TAction>
    class C_AlchemyActionBase: public SDK::wh::framework::C_ParamAction<TParams, TAction> {
      public:
        SDK::wh::playermodule::C_Alchemy *GetAlchemy() const {
            return m_alchemy;
        }

      private:
        SDK::wh::playermodule::C_Alchemy *m_alchemy; // 0x58
        void *m_pSystem0060; // 0x60; base ctor 0x1809CC290 copies the game-modules registry's +0xE0 object's +0xC8 pointer

        static void AssertLayout() {
            static_assert(offsetof(C_AlchemyActionBase, m_alchemy) == 0x58);
            static_assert(offsetof(C_AlchemyActionBase, m_pSystem0060) == 0x60);
        }
    };

    static_assert(sizeof(C_AlchemyActionBase<S_EmptyParams, SDK::wh::framework::C_Action<I_AlchemyAction>>) == 0x68);
} // namespace SDK::wh::playermodule
