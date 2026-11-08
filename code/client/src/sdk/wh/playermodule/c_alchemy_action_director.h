#pragma once

#include "../framework/c_action_director.h"

namespace SDK::wh::playermodule {
    /** RTTI at 0x183BFA5B0 proves the single base at offset zero. */
    class C_AlchemyActionDirector: public SDK::wh::framework::C_ActionDirector {
      public:
        ~C_AlchemyActionDirector() override = default;
        /** SelectObject and Update query director slot 1 for interaction work. */
        bool HasInteractionAction() const {
            return HasAction(kInteractionSlot);
        }

        const SDK::wh::framework::I_Action *GetInteractionAction() const {
            return GetAction(kInteractionSlot);
        }
        SDK::wh::framework::I_Action *GetInteractionAction() {
            return GetAction(kInteractionSlot);
        }

      private:
        static constexpr std::int32_t kInteractionSlot = 1;
    };

    static_assert(sizeof(C_AlchemyActionDirector) == 0x90);
} // namespace SDK::wh::playermodule
