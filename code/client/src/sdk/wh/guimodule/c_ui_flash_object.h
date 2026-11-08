#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK::wh::guimodule {
    /**
     * Base of every row the game hands the flash movies as a data table --
     * items, quests, map POIs and the rest. A plain RTTR value object:
     * `C_UIFlashBase::SetFlashArray` walks its reflected properties into the
     * arguments the movie reads, so nothing here is registered anywhere.
     *
     * Constructed at 0x1807EA8AC, which copies the id and localizes the name.
     * RTTR calls the two fields `Id` and `Name`.
     */
    class C_UIFlashObject {
      public:
        C_UIFlashObject()                                   = delete;
        ~C_UIFlashObject()                                  = delete;
        C_UIFlashObject(const C_UIFlashObject &)            = delete;
        C_UIFlashObject &operator=(const C_UIFlashObject &) = delete;

        const char *GetId() const {
            return m_id.m_pData != nullptr ? m_id.m_pData : "";
        }

        const char *GetName() const {
            return m_name.m_pData != nullptr ? m_name.m_pData : "";
        }

        /**
         * Replaces the name through the engine's own string assignment. Not
         * display text: the screens split a name into words and resolve each
         * as an '@' localization key (0x1803C235C), so plain text shows as
         * "@word @word". Set a single key the localization hook answers.
         */
        void SetName(const char *name) {
            m_name.Assign(name);
        }

      private:
        const void *m_pVTable; // 0x00, 0x183A55268

        CryStringT<char> m_id;   // 0x08
        CryStringT<char> m_name; // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_UIFlashObject, m_id) == 0x08, "C_UIFlashObject::m_id offset mismatch");
            static_assert(offsetof(C_UIFlashObject, m_name) == 0x10, "C_UIFlashObject::m_name offset mismatch");
        }
    };

    static_assert(sizeof(C_UIFlashObject) == 0x18, "C_UIFlashObject size mismatch");
} // namespace SDK::wh::guimodule
