#pragma once

#include "../c_wh_global_environment.h"
#include "../shared/c_signal.h"
#include "c_dialog_module.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::dialogmodule {
    /**
     * The game's own conversation manager.
     *
     * Only what has been proven is named. The offsets come from
     * `C_DialogManager::GetPlayerDialog` (`0x18197CB94`), which reads the
     * instance at `+0x90` and hands back its `I_Dialog` subobject at `+0x18`,
     * and from `AcceptChatRequestForSoul` / `FindDialogForSoul`, which walk the
     * two instance lists. `sizeof` is the 496 bytes `C_DialogModule::Init`
     * (`0x180E2F1E0`) allocates for it.
     *
     * Everything between the named members is opaque on purpose: this exists to
     * answer whether the player is talking to somebody, not to drive the game's
     * dialogue system.
     */
    class C_DialogManager final {
      public:
        /**
         * The manager, or null before the game has built its modules.
         *
         * Both hops are typed: `C_GlobalEnvironment` holds `C_DialogModule` at
         * `+0x150` and the module holds this at `+0x10`, each asserted in the
         * class that owns it. The module-construction function at `0x180C9DA44`
         * is what proves the first, and it is the same table that puts
         * `C_PlayerModule` at the `+0x128` the mod already relies on.
         */
        static C_DialogManager *GetInstance() {
            C_GlobalEnvironment *environment = C_GlobalEnvironment::GetInstance();
            if (environment == nullptr) {
                return nullptr;
            }
            C_DialogModule *module = environment->GetDialogModule();
            return module != nullptr ? module->GetDialogManager() : nullptr;
        }

        bool IsPlayerInDialog() const {
            return m_pPlayerDialog != nullptr;
        }

        /** The running C_DialogInstance the player is in, for comparing by identity. */
        const void *GetPlayerDialog() const {
            return m_pPlayerDialog;
        }

      private:
        // Typed from the ctor 0x180E2FEFC. Embedded objects are sized by the next written offset.
        void *m_vtable;                             // 0x00
        SDK::wh::shared::C_Signal m_signals0008[4]; // 0x08, dispatch unk_185666CF8
        SDK::wh::shared::C_Signal m_signals0048[2]; // 0x48, dispatch unk_185666F50
        std::byte m_object0068[0x8];                // 0x68, built by 0x180E2FDE4
        void *m_pState0070;                         // 0x70, a 0x18-byte object built by 0x180E2FD44
        std::int32_t m_state0078;                   // 0x78, ctor sets 1
        std::byte m_unset007C[0x4];                 // 0x7C, not written by the ctor
        void *m_pSetHead0080;                       // 0x80, std::set head node
        std::size_t m_setSize0088;                  // 0x88
        /** The running C_DialogInstance; its I_Dialog subobject is at +0x18. */
        void *m_pPlayerDialog; // 0x90
        std::uint64_t m_state0098[0xB];     // 0x98, ctor zeroes; the instance lists at 0xC0 and 0xD8 live here
        void *m_pListProxy00F0;             // 0x0F0, a 0x10-byte container proxy pointing back at +0xF0
        std::uint64_t m_state00F8[6];       // 0x0F8, ctor zeroes
        void *m_pHandler0128;               // 0x128, a 0x78-byte object built by 0x180E30ACC with this manager
        std::byte m_object0130[0x48];       // 0x130, built by 0x180E30120
        std::byte m_requestTable0178[0x18]; // 0x178, built by 0x180E3014C
        std::byte m_object0190[0x18];       // 0x190
        std::uint64_t m_state01A8;          // 0x1A8, ctor zeroes
        std::byte m_unset01B0[0x10];        // 0x1B0, not written by the ctor
        std::uint32_t m_requestId01C0;      // 0x1C0, not written by the ctor
        std::byte m_state01C4[0x8];         // 0x1C4, ctor zeroes as one unaligned qword
        std::uint32_t m_state01CC;          // 0x1CC, ctor zeroes
        std::byte m_unset01D0[0x10];        // 0x1D0, not written by the ctor
        std::byte m_object01E0[0x10];       // 0x1E0, built by 0x1809ED074

        static void AssertLayout() {
            static_assert(offsetof(C_DialogManager, m_signals0048) == 0x48, "C_DialogManager::m_signals0048 offset mismatch");
            static_assert(offsetof(C_DialogManager, m_state0078) == 0x78, "C_DialogManager::m_state0078 offset mismatch");
            static_assert(offsetof(C_DialogManager, m_pListProxy00F0) == 0xF0, "C_DialogManager::m_pListProxy00F0 offset mismatch");
            static_assert(offsetof(C_DialogManager, m_pHandler0128) == 0x128, "C_DialogManager::m_pHandler0128 offset mismatch");
            static_assert(offsetof(C_DialogManager, m_requestTable0178) == 0x178, "C_DialogManager::m_requestTable0178 offset mismatch");
            static_assert(offsetof(C_DialogManager, m_requestId01C0) == 0x1C0, "C_DialogManager::m_requestId01C0 offset mismatch");
            static_assert(offsetof(C_DialogManager, m_object01E0) == 0x1E0, "C_DialogManager::m_object01E0 offset mismatch");
            static_assert(offsetof(C_DialogManager, m_pPlayerDialog) == 0x90, "C_DialogManager::m_pPlayerDialog offset mismatch");
        }
    };

    static_assert(sizeof(C_DialogManager) == 0x1F0, "C_DialogManager size mismatch");
} // namespace SDK::wh::dialogmodule
