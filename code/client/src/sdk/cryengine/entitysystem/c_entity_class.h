#pragma once

#include "i_entity_class.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete descriptor for an entity class registered with CEntitySystem.
     *
     * The table at 0x183B282A0 contains exactly the 25 IEntityClass entries
     * and adds no virtual methods. Both the registration allocation and the
     * scalar deleting destructor prove the complete 0x88-byte size.
     */
    class CEntityClass : public IEntityClass {
      public:
        ~CEntityClass() override = default;

        bool IsScriptLoaded() const { return m_scriptLoaded; }

      private:
        std::uint32_t m_flags;                    // 0x08
        std::byte m_padding000C[0x4];
        const char *m_name;                       // 0x10
        const char *m_scriptFile;                 // 0x18
        IEntityScript *m_pEntityScript;           // 0x20
        UserProxyCreateFunc m_userProxyCreateFunc; // 0x28
        void *m_pUserProxyData;                   // 0x30
        bool m_scriptLoaded;                      // 0x38
        std::byte m_padding0039[0x7];
        IEntityPropertyHandler *m_pPropertyHandler;       // 0x40
        IEntityEventHandler *m_pEventHandler;             // 0x48
        IEntityScriptFileHandler *m_pScriptFileHandler;   // 0x50
        SEditorClassInfo m_editorClassInfo;        // 0x58
        std::byte m_entityAttributes[0x8];         // 0x70
        std::byte m_classAttributes[0x8];          // 0x78
        std::uint32_t m_id;                        // 0x80
        std::byte m_padding0084[0x4];
    };

    static_assert(sizeof(CEntityClass) == 0x88, "CEntityClass size mismatch");
} // namespace SDK
