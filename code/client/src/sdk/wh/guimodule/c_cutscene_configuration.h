#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK::wh::guimodule {
    /**
     * One request of a quest graph's `C_CutsceneHandler` to play a cutscene
     * from the cutscene table, as `C_CutscenePlayer::EnqueueCutscene`
     * (0x182B3691C) receives it in a `std::shared_ptr`.
     *
     * Built by 0x182B065F0 inside a `std::_Ref_count_obj2` of 0x38 bytes, so
     * the object is the 0x28 bytes past the control block's 0x10. The name is
     * copied from the cutscene holder; the cutscene is the table row's
     * `I_Cutscene`, null when the name is not in the table, and then the
     * player does nothing with the request.
     */
    class C_CutsceneConfiguration {
      public:
        const SDK::CryStringT<char> &GetName() const {
            return m_name;
        }

        bool HasCutscene() const {
            return m_pCutscene != nullptr;
        }

      private:
        // The holder at 0x00, the owner's id at 0x08, and the handler's
        // AutoPlay and AutoFinish ports at 0x10 and 0x11.
        std::byte m_opaque0000[0x18];
        SDK::CryStringT<char> m_name; // 0x18
        void *m_pCutscene;            // 0x20, wh::guimodule::I_Cutscene

        static void AssertLayout() {
            static_assert(offsetof(C_CutsceneConfiguration, m_name) == 0x18, "C_CutsceneConfiguration::m_name offset mismatch");
            static_assert(offsetof(C_CutsceneConfiguration, m_pCutscene) == 0x20, "C_CutsceneConfiguration::m_pCutscene offset mismatch");
        }
    };

    static_assert(sizeof(C_CutsceneConfiguration) == 0x28, "C_CutsceneConfiguration size mismatch");
} // namespace SDK::wh::guimodule
