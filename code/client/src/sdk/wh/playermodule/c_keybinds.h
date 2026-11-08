#pragma once

#include "../../cryengine/action/i_player_profile_manager.h"
#include "../../cryengine/input/i_input_event_listener.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * The game's binding vocabulary: every superaction the controls menu can
     * show, which action maps each one drives, and which superactions may not
     * share a key.
     *
     * Held by `C_PlayerModule` at +0x40 and built once, on that module's
     * message 0: `C_Keybinds::Init` (0x180601FB4) loads
     * `libs/config/keybindSuperactions.xml` and hands the root to
     * `C_Keybinds::ParseSuperactions` (0x1808FB4CC), which fills the
     * superactions at +0x10, the UI groups at +0x50, the conflict sets at
     * +0x90 and the action-name index at +0x190. The player profile's saved
     * bindings are replayed over the result afterwards, which is why a
     * superaction declared here is rebindable and persists like the game's
     * own.
     *
     * RTTI places IInputEventListener at +0 and IPlayerProfileListener at
     * +0x8. Init registers the latter with AddListener(updateNow=true).
     * LoadFromProfile (+0x10 on the secondary vtable at 0x183F60DF8)
     * reapplies saved keys and defaults to the action maps. The mod uses
     * that callback when its actions were created after the first replay.
     *
     * The deleting destructor at 0x182E14278 releases a 0x1F0-byte object.
     */
    class C_Keybinds: public SDK::IInputEventListener, public SDK::IPlayerProfileListener {
      public:
        /** Replays current choices through the game's callbacks, without saving a file. */
        void ReapplyBindings(SDK::IPlayerProfile *profile) {
            SaveToProfile(profile, false, SDK::IPlayerProfileManager::ePR_Options);
            LoadFromProfile(profile, false, SDK::IPlayerProfileManager::ePR_Options);
        }

      private:
        // Constructor 0x18124417C.
        std::byte m_actionMaps[0x40];        // 0x10; std::unordered_map (load factor 1.0, bucket mask 7/8)
        std::byte m_hashMap0050[0x40];       // 0x50; std::unordered_map (sub_180AD7AD4)
        std::byte m_hashMap0090[0x40];       // 0x90; std::unordered_map (sub_180AD7AD4)
        std::byte m_deviceBindings[3][0x40]; // 0xD0; one 0x40-byte table per device (sub_180AD945C)
        std::byte m_hashMap0190[0x40];       // 0x190; std::unordered_map (sub_180AD7AD4)
        std::int32_t m_value01D0;            // 0x1D0; ctor 3
        std::byte m_padding01D4[0x4];
        const char *m_text01D8;              // 0x1D8; ctor points it at the empty string
        std::byte m_opaque01E0[0x10];        // 0x1E0; not written by the constructor

        static void AssertLayout() {
            static_assert(offsetof(C_Keybinds, m_actionMaps) == 0x10, "C_Keybinds base layout mismatch");
            static_assert(offsetof(C_Keybinds, m_deviceBindings) == 0xD0, "C_Keybinds::m_deviceBindings offset mismatch");
            static_assert(offsetof(C_Keybinds, m_value01D0) == 0x1D0, "C_Keybinds::m_value01D0 offset mismatch");
            static_assert(offsetof(C_Keybinds, m_text01D8) == 0x1D8, "C_Keybinds::m_text01D8 offset mismatch");
        }
    };

    static_assert(sizeof(C_Keybinds) == 0x1F0, "C_Keybinds size mismatch");
} // namespace SDK::wh::playermodule
