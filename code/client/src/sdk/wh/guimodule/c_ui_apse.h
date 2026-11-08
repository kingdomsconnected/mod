#pragma once

#include "../framework/e_ui_apse_view.h"
#include "../shared/c_signal.h"
#include "c_ui_apse_character.h"
#include "c_ui_apse_inventory.h"
#include "c_ui_map.h"
#include "c_ui_quest_log.h"

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The tabbed pause screen. Its pages are embedded rather than pointed at;
     * only the journal, the map and the character page are modelled. From
     * the constructor at 0x18146C89C:
     *
     *   +0x00D8 C_UIApseInventory   +0x1C58 C_UIApseCodex
     *   +0x0BA0 C_UIApsePlayer      +0x1E80 C_UIApseCrafting
     *   +0x10C8 C_UIQuestLog        +0x2158 C_UIApseCharacter
     *   +0x1508 C_UIMap             +0x25C0 C_UIApseCamera
     *
     * The size is not asserted: the object reaches well past the map and its
     * tail is unproven.
     */
    class C_UIApse final {
      public:
        C_UIApse()                            = delete;
        ~C_UIApse()                           = delete;
        C_UIApse(const C_UIApse &)            = delete;
        C_UIApse &operator=(const C_UIApse &) = delete;

        /** Null before the GUI module has built its pages. */
        static C_UIApse *GetInstance();

        /** Show and ChangeView write this observable before the page transition. */
        SDK::wh::framework::E_UIApseView::Type GetRequestedView() const { return m_requestedView; }

        C_UIApseInventory *GetInventory() {
            return reinterpret_cast<C_UIApseInventory *>(m_inventory);
        }

        C_UIMap *GetMap() {
            return &m_map;
        }

        C_UIQuestLog *GetQuestLog() {
            return &m_questLog;
        }

        /**
         * The character page. Held as storage rather than as a member: the
         * class is abstract, being reachable only through its own table.
         */
        C_UIApseCharacter *GetCharacter() {
            return reinterpret_cast<C_UIApseCharacter *>(m_character);
        }

        /**
         * The level's Apse link node: the entity every stage hub hangs off by
         * link name (`CharacterHub`, `FlashPlatesHub`, `AnchorsHub`), as
         * `ForEachApseHubLink` (0x181E26930) walks it. Written only by Lua
         * `UI.OnApseLinkNodeSpawn` (0x180ED52E4); 0 until the level spawns it.
         */
        EntityId GetStageRootEntityId() const {
            return m_stageRootEntityId;
        }

      private:
        // Constructor 0x18146C89C over a 0x2868-byte allocation
        // (sub_18146C7B8). Each model property below is 0x28 bytes: vftable,
        // value, C_Signal at +0x10, eight trailing bytes.
        const void *m_pVTable;                       // 0x00
        void *m_ptr0008;                             // 0x08; ctor 0
        const void *m_pVTables0010[3];               // 0x10; the other C_UIApse tables
        const void *m_pRequestedViewVTable;          // 0x28; C_ModelProperty<E_UIApseView::Type>
        SDK::wh::framework::E_UIApseView::Type m_requestedView; // Observable value at +0x28 + 0x08.
        std::byte m_padding0031[0x7];
        SDK::wh::shared::C_Signal m_requestedViewSignal; // 0x38; dispatch unk_185669208
        std::byte m_requestedViewTail[0x8];          // 0x48
        std::byte m_viewProperty0050[0x28];          // 0x50; a second C_ModelProperty<E_UIApseView::Type>
        std::byte m_apseStateProperty[0x28];         // 0x78; C_ModelProperty<E_UIApseState::Type>
        std::byte m_characterStateProperty[0x28];    // 0xA0; C_ModelProperty<E_UICharacterState::Type>
        void *m_pDebugApse;                    // ICVar, 0xC8, wh_ui_DebugApse
        std::uint32_t m_value00D0;                   // 0xD0; ctor 0
        std::byte m_padding00D4[0x4];
        alignas(8) std::byte m_inventory[sizeof(C_UIApseInventory)]; // 0xD8; wh::guimodule::C_UIApseInventory
        std::byte m_player[0x528];                   // 0xBA0; wh::guimodule::C_UIApsePlayer
        C_UIQuestLog m_questLog; // 0x10C8
        C_UIMap m_map;           // 0x1508
        std::byte m_codex[0x228];                    // 0x1C58; wh::guimodule::C_UIApseCodex (given the crafting page)
        std::byte m_crafting[0x2D8];                 // 0x1E80; wh::guimodule::C_UIApseCrafting
        alignas(8) std::byte m_character[sizeof(C_UIApseCharacter)]; // 0x2158
        std::byte m_camera[0x1A8];                   // 0x25C0; wh::guimodule::C_UIApseCamera
        EntityId m_stageRootEntityId; // 0x2768
        std::byte m_padding276C[0x4];
        std::byte m_infoDialog[0xA8]; // C_UIModalDialogInfo (its sizeof is asserted in c_ui_modal_dialog_info.h), 0x2770; "ApseModalDialog"
        bool m_flag2818;                             // 0x2818; ctor 0
        std::byte m_padding2819[0x7];
        std::uint64_t m_value2820;                   // 0x2820; ctor copies qword_185326308
        std::byte m_opaque2828[0x38];                // 0x2828; not written by the constructor
        void *m_ptr2860;                             // 0x2860; ctor 0

        static void AssertLayout() {
            static_assert(offsetof(C_UIApse, m_requestedView) == 0x30);
            static_assert(offsetof(C_UIApse, m_questLog) == 0x10C8, "C_UIApse::m_questLog offset mismatch");
            static_assert(offsetof(C_UIApse, m_map) == 0x1508, "C_UIApse::m_map offset mismatch");
            static_assert(offsetof(C_UIApse, m_character) == 0x2158, "C_UIApse::m_character offset mismatch");
            static_assert(offsetof(C_UIApse, m_stageRootEntityId) == 0x2768, "C_UIApse::m_stageRootEntityId offset mismatch");
            static_assert(offsetof(C_UIApse, m_requestedViewSignal) == 0x38, "C_UIApse::m_requestedViewSignal offset mismatch");
            static_assert(offsetof(C_UIApse, m_characterStateProperty) == 0xA0, "C_UIApse::m_characterStateProperty offset mismatch");
            static_assert(offsetof(C_UIApse, m_pDebugApse) == 0xC8, "C_UIApse::m_pDebugApse offset mismatch");
            static_assert(offsetof(C_UIApse, m_inventory) == 0xD8, "C_UIApse::m_inventory offset mismatch");
            static_assert(offsetof(C_UIApse, m_player) == 0xBA0, "C_UIApse::m_player offset mismatch");
            static_assert(offsetof(C_UIApse, m_codex) == 0x1C58, "C_UIApse::m_codex offset mismatch");
            static_assert(offsetof(C_UIApse, m_crafting) == 0x1E80, "C_UIApse::m_crafting offset mismatch");
            static_assert(offsetof(C_UIApse, m_camera) == 0x25C0, "C_UIApse::m_camera offset mismatch");
            static_assert(offsetof(C_UIApse, m_infoDialog) == 0x2770, "C_UIApse::m_infoDialog offset mismatch");
            static_assert(offsetof(C_UIApse, m_ptr2860) == 0x2860, "C_UIApse::m_ptr2860 offset mismatch");
        }
    };

    static_assert(sizeof(C_UIApse) == 0x2868, "C_UIApse size mismatch");
} // namespace SDK::wh::guimodule
