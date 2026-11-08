#pragma once

#include "i_entity_archetype.h"

#include <cstddef>

namespace SDK {
    class IXmlNode;

    /**
     * Concrete archetype descriptor used by CEntitySystem's spawn pipeline.
     *
     * Its primary table at 0x184075B80 contains exactly the six
     * IEntityArchetype entries. A secondary _i_reference_target<int>
     * subobject starts at +0x08, and the factory allocation proves the
     * complete 0x38-byte size.
     */
    class CEntityArchetype : public IEntityArchetype {
      public:
        ~CEntityArchetype() override = default;

        /** Payload-only view of the stored XmlNodeRef at +0x28. */
        IXmlNode *GetObjectVarsNodeRaw() const { return m_pObjectVarsNode; }

      private:
        std::byte m_referenceTarget0008[0x10]; // 0x08
        const char *m_name;                    // 0x18
        IScriptTable *m_pProperties;           // 0x20
        IXmlNode *m_pObjectVarsNode;            // 0x28
        IEntityClass *m_pClass;                 // 0x30
    };

    static_assert(sizeof(CEntityArchetype) == 0x38, "CEntityArchetype size mismatch");
} // namespace SDK
