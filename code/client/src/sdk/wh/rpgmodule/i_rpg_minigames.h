#pragma once

#include "c_soul.h"

namespace SDK::wh::rpgmodule {
    /**
     * RTTI names this primary base of C_RPGMinigames. Its vtable begins
     * with gameplay methods, not a destructor. Only the first seventeen
     * slots are declared; no code constructs or destroys this interface.
     * C_Alchemy callback 0x1808981F7 calls slot15 and stores XMM0 at +0x6D0;
     * native Shutdown separately calls slot16.
     */
    class I_RPGMinigames {
      public:
        virtual void Unknown0000()                                                                                                              = 0;
        virtual void Unknown0001()                                                                                                              = 0;
        virtual void Unknown0002()                                                                                                              = 0;
        virtual void Unknown0003()                                                                                                              = 0;
        virtual void Unknown0004()                                                                                                              = 0;
        virtual void Unknown0005()                                                                                                              = 0;
        virtual void Unknown0006()                                                                                                              = 0;
        virtual void Unknown0007()                                                                                                              = 0;
        virtual void Unknown0008()                                                                                                              = 0;
        virtual bool IsAlchemyRecipeKnown(SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t recipe)                                               = 0;
        virtual bool IsAlchemyRecipeStepKnown(SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t recipe, std::uint32_t step)                       = 0;
        virtual bool IsAlchemyRecipeRangeKnown(SDK::wh::rpgmodule::I_Soul *soul, std::uint32_t recipe, std::uint32_t first, std::uint32_t last) = 0;
        virtual void Unknown0012()                                                                                                              = 0;
        virtual void Unknown0013()                                                                                                              = 0;
        virtual void Unknown0014()                                                                                                              = 0;
        virtual float OnAlchemyStarted(SDK::wh::rpgmodule::I_Soul *soul)                                                                        = 0;
        virtual void OnAlchemyFinished(SDK::wh::rpgmodule::I_Soul *soul)                                                                        = 0;
    };

    static_assert(sizeof(I_RPGMinigames) == 0x8);
} // namespace SDK::wh::rpgmodule
