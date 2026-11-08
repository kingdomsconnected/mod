#pragma once

#include "../../types.h"
#include "../dialogmodule/i_ui_hud_interactive_dialog.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The game's interactive-dialogue HUD: the object behind the choice list.
     *
     * It is bound to the `hud` element (movie `hud.gfx`, layer 25), so there is
     * no separate dialogue element to find and nothing to make visible at the
     * element level -- the HUD is already up.
     *
     * The layout is taken from the constructor at `0x181876784`, and every
     * offset is corroborated by the two accessors that use it: they address
     * their fields relative to the `I_UIHudInteractiveDialog` subobject as
     * `iface+0x08`, `+0x10`, `+0x18` and `+0x28`, and reach the flash base by
     * `this-0x58`. `sizeof` is the 160 bytes the allocation at `0x1818766F8`
     * asks for.
     *
     * This deliberately does not inherit its two bases. Their layout is already
     * proven by address, and spelling it as multiple inheritance would hand the
     * compiler a say in something the game has already decided.
     */
    class C_UIHudInteractiveDialog final {
      public:
        /** The contract the game's own dialogue system drives this object through. */
        dialogmodule::I_UIHudInteractiveDialog *GetInterface() {
            return reinterpret_cast<dialogmodule::I_UIHudInteractiveDialog *>(&m_interfaceVtable);
        }

        /**
         * Empties the staged rows, as `std::vector::clear` would.
         *
         * This is needed because `SetDialogSequences` does **not** do it: the
         * flush reads the rows and leaves them staged, so a second page would
         * draw the first page's rows as well as its own. The game's own dialogue
         * system must clear them somewhere outside this interface; this does the
         * same job without having found where.
         *
         * Each row owns two `CryStringT<char>`s, and both are released through
         * the engine's own assignment operator -- so every free happens inside
         * the game, against the heap that allocated it. The buffer itself is
         * kept, which is what clearing a vector means.
         */
        void ClearStagedRows() {
            for (std::uint8_t *row = m_rowsBegin; row != nullptr && row < m_rowsEnd; row += kRowStride) {
                reinterpret_cast<SDK::CryStringT<char> *>(row)->Assign("");
                reinterpret_cast<SDK::CryStringT<char> *>(row + 0x10)->Assign("");
            }
            m_rowsEnd = m_rowsBegin;
        }

      private:
        /** One staged row: text, enabled, secondary text, two ints and the money flag. */
        static constexpr std::ptrdiff_t kRowStride = 0x28;

        std::byte m_flashBase[0x58];      // 0x00, a C_UIFlashBase (sizeof 0x58)
        void *m_interfaceVtable;          // 0x58, the I_UIHudInteractiveDialog subobject
        bool m_emitSkillChecks;           // 0x60, read as iface+0x08
        std::byte m_padding0061[0x7];     //
        std::uint8_t *m_rowsBegin;        // 0x68, iface+0x10
        std::uint8_t *m_rowsEnd;          // 0x70, iface+0x18
        std::uint8_t *m_rowsCapacity;     // 0x78
        std::uint8_t *m_skillChecksBegin; // 0x80, iface+0x28
        std::uint8_t *m_skillChecksEnd;   // 0x88
        void *m_opaque0090;               // 0x90
        int m_opaque0098;                 // 0x98
        std::byte m_padding009C[0x4];     //

        static void AssertLayout() {
            static_assert(offsetof(C_UIHudInteractiveDialog, m_interfaceVtable) == 0x58, "C_UIHudInteractiveDialog::m_interfaceVtable offset mismatch");
            static_assert(offsetof(C_UIHudInteractiveDialog, m_emitSkillChecks) == 0x60, "C_UIHudInteractiveDialog::m_emitSkillChecks offset mismatch");
            static_assert(offsetof(C_UIHudInteractiveDialog, m_rowsBegin) == 0x68, "C_UIHudInteractiveDialog::m_rowsBegin offset mismatch");
            static_assert(offsetof(C_UIHudInteractiveDialog, m_rowsEnd) == 0x70, "C_UIHudInteractiveDialog::m_rowsEnd offset mismatch");
            static_assert(offsetof(C_UIHudInteractiveDialog, m_skillChecksBegin) == 0x80, "C_UIHudInteractiveDialog::m_skillChecksBegin offset mismatch");
            static_assert(offsetof(C_UIHudInteractiveDialog, m_opaque0098) == 0x98, "C_UIHudInteractiveDialog::m_opaque0098 offset mismatch");
        }
    };

    static_assert(sizeof(C_UIHudInteractiveDialog) == 0xA0, "C_UIHudInteractiveDialog size mismatch");
} // namespace SDK::wh::guimodule
