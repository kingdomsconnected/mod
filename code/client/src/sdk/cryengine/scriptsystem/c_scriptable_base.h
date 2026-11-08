#pragma once

#include "i_script_system.h"
#include "i_script_table.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Global RTTI CScriptableBase, vtable 0x183AA4AD8. Alchemy's RTTI
     * contains this single base at zero. Initializer 0x18144CE0C writes the
     * table/system/bind id; the derived allocation places its first member
     * at 0x60. Unknown virtuals must not be called through placeholders.
     */
    class CScriptableBase {
      public:
        virtual ~CScriptableBase() = default;
        virtual void Opaque01()    = 0;
        virtual void Opaque02()    = 0;
        virtual void Opaque03()    = 0;

        SDK::IScriptTable *GetMethodTable() const {
            return m_methodTable;
        }
        SDK::IScriptSystem *GetScriptSystem() const {
            return m_scriptSystem;
        }

      private:
        char m_sGlobalName[64]; // 0x08, written by SetGlobalName 0x180B85140, tested by Done 0x1819D5648
        SDK::IScriptTable *m_methodTable;
        SDK::IScriptSystem *m_scriptSystem;
        std::int32_t m_nParamIdOffset;
        std::byte m_opaque005C[0x4]; // 0x5C, tail padding

        static void AssertLayout() {
            static_assert(offsetof(CScriptableBase, m_sGlobalName) == 0x08);
            static_assert(offsetof(CScriptableBase, m_methodTable) == 0x48);
            static_assert(offsetof(CScriptableBase, m_scriptSystem) == 0x50);
            static_assert(offsetof(CScriptableBase, m_nParamIdOffset) == 0x58);
        }
    };

    static_assert(sizeof(CScriptableBase) == 0x60);
} // namespace SDK
