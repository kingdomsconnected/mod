#pragma once

#include <cstddef>

namespace SDK {
    /**
     * Borrowed native interned name. CryEngine calls this type ActionId in
     * action interfaces. KCD2's menu OnAction reads the string at +0x00;
     * CActionMapAction::GetActionId returns its embedded name at +0x40.
     * Construction, copying and destruction require the game's name table.
     */
    class CCryName {
      public:
        CCryName()                            = delete;
        CCryName(const CCryName &)            = delete;
        CCryName &operator=(const CCryName &) = delete;
        ~CCryName()                           = delete;

        const char *c_str() const { return m_str != nullptr ? m_str : ""; }

      private:
        const char *m_str;

        static void AssertLayout() { static_assert(offsetof(CCryName, m_str) == 0x00, "CCryName::m_str offset mismatch"); }
    };

    static_assert(sizeof(CCryName) == 0x08, "CCryName size mismatch");
} // namespace SDK
