#pragma once

#include "../entitymodule/c_body_part_data.h"
#include "../shared/c_signal.h"
#include "s_missile_hit_data.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    class I_Soul;

    class I_CombatSoul {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        // Reflected signature and thunk prove slot 27 (180EE031C).
        virtual void TakeDamage(float staminaDamage, float healthDamage, I_Soul *attacker, bool flag, const wh::entitymodule::C_BodyPartData &bodyPart) = 0;
        virtual void unk_fn0028()                                                                                                                       = 0;
        virtual void unk_fn0029()                                                                                                                       = 0;
        virtual void unk_fn0030()                                                                                                                       = 0;
        virtual void unk_fn0031()                                                                                                                       = 0;
        virtual void unk_fn0032()                                                                                                                       = 0;
        virtual void unk_fn0033()                                                                                                                       = 0;
        virtual void unk_fn0034()                                                                                                                       = 0;
        virtual void unk_fn0035()                                                                                                                       = 0;
        virtual void unk_fn0036()                                                                                                                       = 0;
        virtual void unk_fn0037()                                                                                                                       = 0;
        virtual void unk_fn0038()                                                                                                                       = 0;
        virtual void unk_fn0039()                                                                                                                       = 0;
        virtual void unk_fn0040()                                                                                                                       = 0;
        virtual void unk_fn0041()                                                                                                                       = 0;
        virtual void unk_fn0042()                                                                                                                       = 0;

        // Slot 43: the member return handle occupies RDX.
        virtual S_EventHandle182D291D0 *ProcessMissileHit(S_EventHandle182D291D0 *result, const S_MissileHitData &data) = 0;
    };

    static_assert(sizeof(I_CombatSoul) == 8);

    class C_CombatSoul: public I_CombatSoul {
      public:
        I_Soul *GetSoul() const {
            return m_pSoul;
        }

      private:
        // Constructor 0x18070F6CC.
        shared::C_Signal m_signals0008[7]; // 0x08; dispatches unk_185665AC0, unk_185665430, unk_185669860, unk_185669890, unk_185669880, unk_1856698E0, unk_1856698D0
        I_Soul *m_pSoul;                   // 0x78
        void *m_ptrs0080[2];               // 0x80; ctor 0
        std::int32_t m_value0090;          // 0x90; ctor 0
        std::byte m_padding0094[0x4];
        void *m_pOwned0098;                // 0x98; owning pointer from sub_1803F2814, released through its vtable slot 0
        void *m_pOwnedTree00A0;            // 0xA0; owning pointer to a std::map-backed object from sub_18117A950

        static void AssertLayout() {
            static_assert(offsetof(C_CombatSoul, m_pSoul) == 0x78);
            static_assert(offsetof(C_CombatSoul, m_signals0008) == 0x08);
            static_assert(offsetof(C_CombatSoul, m_value0090) == 0x90);
            static_assert(offsetof(C_CombatSoul, m_pOwned0098) == 0x98);
        }
    };

    static_assert(sizeof(C_CombatSoul) == 0xA8);
} // namespace SDK::wh::rpgmodule
