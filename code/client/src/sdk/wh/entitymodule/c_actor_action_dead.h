#pragma once

#include "c_actor.h"
#include "c_actor_action_item_handling_anim.h"

namespace SDK::wh::entitymodule {
    /** RTTI-proven secondary interface at C_ActorActionDead+0x68. */
    class I_DisableRagdollEffect {
      public:
        virtual void unk_fn0000() = 0;
    };
    static_assert(sizeof(I_DisableRagdollEffect) == 0x08);

    /** Factories 0x1812C0AD8 and 0x182873E64 both allocate 0x78 bytes. */
    class C_ActorActionDead: public C_ActorAction<I_ActorAction, framework::C_Action<I_ActorAction>>, public I_DisableRagdollEffect {
      public:
        C_Actor *GetActor() const {
            return GetActionActor()->GetActor();
        }

      private:
        // Ctor 0x1812C0B74 zeroes 0x70..0x71 as one word.
        std::uint8_t m_state0070;     // 0x70
        std::uint8_t m_flag0071;      // 0x71, Enter (0x180E78768) skips a body-actor call when set
        std::byte m_padding0072[0x6];

        static void AssertLayout() {
            static_assert(offsetof(C_ActorActionDead, m_flag0071) == 0x71, "C_ActorActionDead::m_flag0071 offset mismatch");
        }
    };
    static_assert(sizeof(C_ActorActionDead) == 0x78, "C_ActorActionDead size mismatch");
} // namespace SDK::wh::entitymodule
