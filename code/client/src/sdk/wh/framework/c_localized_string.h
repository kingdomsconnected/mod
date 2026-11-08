#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK::wh::framework {
    /**
     * A piece of text the game stores as a key and resolves on display.
     *
     * Reflection registers exactly one property on it -- `StringName`, at
     * offset 8 -- and `Resolve` reads nothing else: it copies that string out
     * and answers whether localisation was available, leaving the caller to
     * decide whether to push the result through `wh::framework::Localize`.
     * The eight bytes in front of it are not touched by either and are left
     * opaque.
     *
     * Literal text can be stored here. The localiser rewrites only the tokens
     * it recognises and passes everything else through, so a sentence the
     * server authored survives the round trip; a key it would recognise would
     * not.
     */
    class C_LocalizedString {
      public:
        const SDK::CryStringT<char> &GetStringName() const {
            return m_stringName;
        }

        /** Assigns through the engine's own string operator, so the buffer stays the game's. */
        void SetStringName(const char *value) {
            m_stringName.Assign(value);
        }

      private:
        // A vtable, not padding: `C_LogBase`'s constructor writes
        // `&C_LocalizedString::vftable` into the embedded one at its +0x18,
        // whose string then lands at +0x20.
        void *m_vtable;                     // 0x00
        SDK::CryStringT<char> m_stringName; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(C_LocalizedString, m_stringName) == 0x08, "C_LocalizedString::m_stringName offset mismatch");
        }
    };

    static_assert(sizeof(C_LocalizedString) == 0x10, "C_LocalizedString size mismatch");
} // namespace SDK::wh::framework
