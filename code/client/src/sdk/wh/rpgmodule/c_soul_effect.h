#pragma once

#include "../framework/wuid.h"
#include "c_event.h"

#include <cstddef>

namespace SDK::wh::rpgmodule {
    /**
     * An effect about one soul. Constructor 0x1809DC974: the `C_Effect` base
     * registers the effect on its event, then this records the subject.
     */
    class C_SoulEffect: public C_Effect {
      public:
        /** The soul this effect changes. */
        framework::WUID GetSubjectSoulWuid() const {
            return m_subjectSoulWuid;
        }

      private:
        framework::WUID m_subjectSoulWuid; // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_SoulEffect, m_subjectSoulWuid) == 0x10, "C_SoulEffect::m_subjectSoulWuid offset mismatch");
        }
    };

    static_assert(sizeof(C_SoulEffect) == 0x18, "C_SoulEffect size mismatch");
} // namespace SDK::wh::rpgmodule
