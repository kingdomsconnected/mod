#pragma once

#include <cstddef>

namespace SDK::wh::dialogmodule {
    class C_DialogManager;

    /**
     * The dialogue module, as `C_GlobalEnvironment` holds it.
     *
     * Layout from its constructor at `0x18120FEB8`, which installs the vtable
     * and zeroes the int, and from `Init` at `0x180E2F1E0`, which allocates the
     * manager and stores it at `+0x10`. `sizeof` is the 32 bytes the module
     * construction at `0x180C9DA44` asks for.
     */
    class C_DialogModule final {
      public:
        /** The conversation manager, or null before `Init` has run. */
        C_DialogManager *GetDialogManager() const {
            return m_pDialogManager;
        }

      private:
        void *m_vtable;                    // 0x00
        int m_moduleState0008;             // 0x08, C_BaseModule-style state; the ctor (0x18120FEB8) sets 0
        std::byte m_padding000C[0x4];      //
        C_DialogManager *m_pDialogManager; // 0x10
        std::byte m_unset0018[0x8];        // 0x18, written by neither the ctor nor Init (0x180E2F1E0)

        static void AssertLayout() {
            static_assert(offsetof(C_DialogModule, m_moduleState0008) == 0x08, "C_DialogModule::m_moduleState0008 offset mismatch");
            static_assert(offsetof(C_DialogModule, m_pDialogManager) == 0x10, "C_DialogModule::m_pDialogManager offset mismatch");
        }
    };

    static_assert(sizeof(C_DialogModule) == 0x20, "C_DialogModule size mismatch");
} // namespace SDK::wh::dialogmodule
