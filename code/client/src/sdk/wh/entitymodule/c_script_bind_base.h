#pragma once

#include "../../cryengine/scriptsystem/i_script_table.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The shared prefix of the entity module's Lua script binds.
     *
     * This SDK type models a shared prefix, not a native base class. Every bind the
     * registrar at 0x180E30204 builds is 0x70 bytes, runs the same initializer
     * at 0x18144CE0C, and uses these fields identically: +0x48 the IScriptTable
     * the bind registers its Lua functions into, +0x50 the script system,
     * +0x58 an integer copied into every registration record, +0x68 the
     * ISystem. The initializer is CryEngine's `CScriptableBase::Init`, so
     * fields from +0x08 hold its 64-byte global name, methods table,
     * script system and parameter-id offset.
     */
    struct ScriptBindPrefixLayout {
        char m_globalName[0x40];       // 0x08, CScriptableBase::m_sGlobalName
        IScriptTable *m_pMethodTable;  // 0x48, CScriptableBase::m_pMethodsTable
        void *m_pScriptSystem;         // 0x50, CScriptableBase::m_pSS
        std::int32_t m_bindId;         // 0x58, CScriptableBase::m_nParamIdOffset
        std::byte m_padding005C[0x4];  // 0x5C
        void *m_state0060;             // 0x60
        void *m_pSystem;               // 0x68
    };

    static_assert(sizeof(ScriptBindPrefixLayout) == 0x68, "ScriptBindPrefixLayout size mismatch");
    static_assert(offsetof(ScriptBindPrefixLayout, m_globalName) == 0x00, "ScriptBindPrefixLayout::m_globalName offset mismatch");
    static_assert(offsetof(ScriptBindPrefixLayout, m_pMethodTable) == 0x40, "ScriptBindPrefixLayout::m_pMethodTable offset mismatch");
    static_assert(offsetof(ScriptBindPrefixLayout, m_pScriptSystem) == 0x48, "ScriptBindPrefixLayout::m_pScriptSystem offset mismatch");
    static_assert(offsetof(ScriptBindPrefixLayout, m_bindId) == 0x50, "ScriptBindPrefixLayout::m_bindId offset mismatch");
    static_assert(offsetof(ScriptBindPrefixLayout, m_padding005C) == 0x54, "ScriptBindPrefixLayout::m_padding005C offset mismatch");
    static_assert(offsetof(ScriptBindPrefixLayout, m_state0060) == 0x58, "ScriptBindPrefixLayout::m_state0060 offset mismatch");
    static_assert(offsetof(ScriptBindPrefixLayout, m_pSystem) == 0x60, "ScriptBindPrefixLayout::m_pSystem offset mismatch");
} // namespace SDK::wh::entitymodule
