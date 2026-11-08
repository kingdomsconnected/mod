#pragma once

#include "../shared/c_signal.h"
#include "c_ai_brain.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::xgenaimodule {
    /**
     * What `C_IntelligentObjectManager` keys by WUID: the XGenAI object that
     * owns a brain -- an NPC's, and a smart object's such as a fireplace.
     *
     * Only the brain is claimed. `XGenAIModule.GetBrainVariable` and
     * `SetBrainVariable` (0x183421CE8, 0x1834239D4) and 0x18124E0BC all read
     * it at `+0x70` and use it as a `C_AIBrain`. Never constructed by the mod.
     */
    class C_IntelligentObject {
      public:
        C_AIBrain *GetBrain() const {
            return m_pBrain;
        }

      private:
        std::byte m_base0000[0x60];          // 0x00, base sub-object built by 0x180A2BD74; the ctor (0x180A2BC4C) writes vptrs at 0x00 and 0x20
        SDK::wh::shared::C_Signal m_signal0060; // 0x60, dispatch unk_185665430
        C_AIBrain *m_pBrain;                 // 0x70, ctor zeroes
        std::byte m_scriptTable0078[0x8];    // 0x78, SmartScriptTable of the entity script
        std::uint64_t m_state0080[3];        // 0x80, ctor zeroes
        std::uint64_t m_wuid0098;            // 0x98, the WUID the manager keys this by
        std::uint8_t m_flag00A0;             // 0xA0, ctor sets 0
        std::uint8_t m_flag00A1;             // 0xA1, ctor sets 0

        static void AssertLayout() {
            static_assert(offsetof(C_IntelligentObject, m_signal0060) == 0x60, "C_IntelligentObject::m_signal0060 offset mismatch");
            static_assert(offsetof(C_IntelligentObject, m_pBrain) == 0x70, "C_IntelligentObject::m_pBrain offset mismatch");
            static_assert(offsetof(C_IntelligentObject, m_wuid0098) == 0x98, "C_IntelligentObject::m_wuid0098 offset mismatch");
        }
    };
} // namespace SDK::wh::xgenaimodule
