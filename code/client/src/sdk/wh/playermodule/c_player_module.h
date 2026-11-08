#pragma once

#include "c_fow_manager.h"
#include "c_keybinds.h"
#include "c_tutorial_manager.h"
#include "i_action_sets.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    class C_ActionHintManager;
    class C_FastTravel;

    /**
     * C_GlobalEnvironment +0x128. The constructor (0x180BE9208) allocates
     * each manager in turn; only the ones below are mapped.
     */
    class C_PlayerModule final {
      public:
        C_PlayerModule()                                  = delete;
        ~C_PlayerModule()                                 = delete;
        C_PlayerModule(const C_PlayerModule &)            = delete;
        C_PlayerModule &operator=(const C_PlayerModule &) = delete;

        C_FastTravel *GetFastTravel() const {
            return m_pFastTravel;
        }

        C_FOWManager *GetFOWManager() const {
            return m_pFOWManager;
        }

        C_ActionHintManager *GetActionHintManager() const {
            return m_pActionHintManager;
        }

        /** The `I_ActionSets` base of the module's `C_ActionSets`, as the constructor stores it. */
        I_ActionSets *GetActionSets() const {
            return m_pActionSets;
        }

      private:
        std::byte m_vftable0000[0x8];                    // 0x00; C_PlayerModule vftable.
        std::uint32_t m_value0008;                        // 0x08; ctor 0x180BE9208 writes 0.
        std::byte m_padding000C[0x4];
        std::byte m_vftable0010[0x8];                    // 0x10; second C_PlayerModule vftable.
        void *m_pManager0018;                             // 0x18; 0xB0-byte object built by sub_180EF7184.
        C_TutorialManager *m_pTutorialManager;            // 0x20; 0x50 bytes.
        C_FastTravel *m_pFastTravel;                      // 0x28; 0x130 bytes.
        void *m_pRandomEventManager;                      // 0x30; C_RandomEventManager, 0x118 bytes.
        C_FOWManager *m_pFOWManager;                      // 0x38; 0xA8 bytes.
        C_Keybinds *m_pKeybinds;                          // 0x40; 0x1F0 bytes.
        void *m_pObject0048;                              // 0x48; 0x20-byte {u32, ptr, C_Signal unk_1856694B8}.
        void *m_pObject0050;                              // 0x50; 0x50-byte object opening with C_Signal unk_185665430.
        C_ActionHintManager *m_pActionHintManager;        // 0x58; 0x140 bytes.
        I_ActionSets *m_pActionSets;                      // 0x60; C_ActionSets (0x90) + 8.
        void *m_pObject0068;                              // 0x68; 0x20 bytes.
        void *m_pObject0070;                              // 0x70; 0x10 bytes.
        void *m_pPlayerManager;                           // 0x78; C_PlayerManager, 0xD8 bytes.
        void *m_pShelverManager;                          // 0x80; C_ShelverManager, 0x88 bytes.
        void *m_pControllerFeedbackManager;               // 0x88; C_ControllerFeedbackManager, 0x58 bytes.
        void *m_pForgeWardrobeManager;                    // 0x90; C_ForgeWardrobeManager, 0x20 bytes.
        void *m_ptr0098;                                  // 0x98; ctor writes 0.

        static void AssertLayout() {
            static_assert(offsetof(C_PlayerModule, m_pFastTravel) == 0x28, "C_PlayerModule::m_pFastTravel offset mismatch");
            static_assert(offsetof(C_PlayerModule, m_pActionHintManager) == 0x58, "C_PlayerModule::m_pActionHintManager offset mismatch");
            static_assert(offsetof(C_PlayerModule, m_pActionSets) == 0x60, "C_PlayerModule::m_pActionSets offset mismatch");
            static_assert(offsetof(C_PlayerModule, m_pTutorialManager) == 0x20, "C_PlayerModule::m_pTutorialManager offset mismatch");
            static_assert(offsetof(C_PlayerModule, m_pFOWManager) == 0x38, "C_PlayerModule::m_pFOWManager offset mismatch");
            static_assert(offsetof(C_PlayerModule, m_pKeybinds) == 0x40, "C_PlayerModule::m_pKeybinds offset mismatch");
            static_assert(offsetof(C_PlayerModule, m_pPlayerManager) == 0x78, "C_PlayerModule::m_pPlayerManager offset mismatch");
            static_assert(offsetof(C_PlayerModule, m_ptr0098) == 0x98, "C_PlayerModule::m_ptr0098 offset mismatch");
        }
    };
} // namespace SDK::wh::playermodule
