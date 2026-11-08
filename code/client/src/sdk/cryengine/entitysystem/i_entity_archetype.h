#pragma once

#include "../../types.h"

namespace SDK {
    class IEntityClass;
    class IScriptTable;

    /**
     * IEntityArchetype, vtable 0x183BD7920. The KCD2 interface has six slots;
     * its class/name/property getters and XML loader are proven against the
     * concrete CEntityArchetype implementations. GetObjectVars remains opaque
     * because its non-trivial XmlNodeRef return ABI is not represented safely
     * by this SDK's payload-only handle.
     */
    class IEntityArchetype {
      public:
        virtual ~IEntityArchetype() = default; // +0x00
        virtual IEntityClass *GetClass() const = 0; // +0x08
        virtual const char *GetName() const = 0; // +0x10
        virtual IScriptTable *GetProperties() = 0; // +0x18
        virtual void unk_fn0004() = 0;
        virtual void LoadFromXML(XmlNodeRef &propertiesNode,
                                 XmlNodeRef &objectVarsNode) = 0; // +0x28
    };

    static_assert(sizeof(IEntityArchetype) == 0x8, "IEntityArchetype size mismatch");
} // namespace SDK
