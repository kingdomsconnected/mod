#pragma once

#include "c_actor_material_effects.h"

namespace SDK::wh::entitymodule {
    class C_Player;

    /**
     * Local-player specialization of actor material effects.
     *
     * Its RTTI-proven table at 0x183A81678 has exactly the four inherited
     * C_ActorMaterialEffects slots and adds no virtual operations. The inline
     * constructor stores the owning C_Player at +0xB0, while its deleting
     * destructor proves the complete 0xB8-byte layout.
     */
    class C_PlayerMaterialEffects : public C_ActorMaterialEffects {
      public:
        ~C_PlayerMaterialEffects() override = default;
        void unk_fn0001() override;
        void unk_fn0002() override;
        void unk_fn0003() override;

      private:
        C_Player *m_pPlayer; // 0xB0
    };

    static_assert(sizeof(C_PlayerMaterialEffects) == 0xB8,
                  "C_PlayerMaterialEffects size mismatch");
} // namespace SDK::wh::entitymodule
