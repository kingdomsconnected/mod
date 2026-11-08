#pragma once

#include <cstddef>

namespace SDK::wh::dialogmodule {
    /**
     * The handle a conversation is addressed by: a decision's `Alias`.
     *
     * Two qwords, passed by `const &` -- the binary's own mangled
     * `wh::xgenaimodule::RequestMonologue` (0x1804C94E4) spells the type and
     * the reference. The Lua binds build it on the stack as the alias string
     * followed by a null, and `C_Human::RequestDialog` (0x18093AE58) copies
     * both qwords into `CryStringT`s, so the second is a string as well; what
     * it names is not proven.
     */
    struct S_DecisionAlias {
        const char *m_szName;    // 0x00
        const char *m_szUnk0008; // 0x08
    };

    static_assert(sizeof(S_DecisionAlias) == 0x10, "S_DecisionAlias size mismatch");
    static_assert(offsetof(S_DecisionAlias, m_szName) == 0x00, "S_DecisionAlias::m_szName offset mismatch");
    static_assert(offsetof(S_DecisionAlias, m_szUnk0008) == 0x08, "S_DecisionAlias::m_szUnk0008 offset mismatch");
} // namespace SDK::wh::dialogmodule
