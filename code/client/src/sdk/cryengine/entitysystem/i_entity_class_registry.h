#pragma once

#include "i_entity_class.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class IEntityClassRegistryListener;

    /**
     * IEntityClassRegistry, vtable 0x184075920; CEntityClassRegistry's
     * implementation is at 0x184075988. All 12 slots are mapped in their
     * KCD2 order.
     */
    class IEntityClassRegistry {
      public:
        /** Complete descriptor consumed by RegisterStdClass. */
        struct SEntityClassDesc {
            std::uint32_t flags = 0;                              // 0x00
            std::uint32_t padding0004 = 0;
            const char *name = nullptr;                           // 0x08
            const char *scriptFile = nullptr;                     // 0x10
            IEntityScript *entityScript = nullptr;                // 0x18
            SEditorClassInfo editorClassInfo{};                   // 0x20
            IEntityClass::UserProxyCreateFunc userProxyCreateFunc = nullptr; // 0x38
            void *userProxyData = nullptr;                        // 0x40
            IEntityPropertyHandler *propertyHandler = nullptr;    // 0x48
            IEntityEventHandler *eventHandler = nullptr;          // 0x50
            IEntityScriptFileHandler *scriptFileHandler = nullptr;// 0x58
            IEntityAttributeArray *classAttributes = nullptr;     // 0x60
            IEntityAttributeArray *entityAttributes = nullptr;    // 0x68
        };

        virtual ~IEntityClassRegistry() = default; // +0x00
        virtual bool RegisterEntityClass(IEntityClass *entityClass) = 0;
        virtual bool UnregisterEntityClass(IEntityClass *entityClass) = 0;
        virtual IEntityClass *FindClass(const char *className) const = 0;
        virtual IEntityClass *GetDefaultClass() const = 0;
        virtual void LoadClasses(const char *path, bool onlyNewClasses = false) = 0;
        virtual IEntityClass *RegisterStdClass(const SEntityClassDesc &entityClassDesc) = 0;
        virtual void RegisterListener(IEntityClassRegistryListener *listener) = 0;
        virtual void UnregisterListener(IEntityClassRegistryListener *listener) = 0;
        virtual void IteratorMoveFirst() = 0;
        virtual IEntityClass *IteratorNext() = 0;
        virtual int GetClassCount() const = 0;
    };

    static_assert(offsetof(IEntityClassRegistry::SEntityClassDesc, name) == 0x08,
                  "SEntityClassDesc::name offset mismatch");
    static_assert(offsetof(IEntityClassRegistry::SEntityClassDesc,
                           editorClassInfo) == 0x20,
                  "SEntityClassDesc::editorClassInfo offset mismatch");
    static_assert(offsetof(IEntityClassRegistry::SEntityClassDesc,
                           userProxyCreateFunc) == 0x38,
                  "SEntityClassDesc::userProxyCreateFunc offset mismatch");
    static_assert(offsetof(IEntityClassRegistry::SEntityClassDesc,
                           classAttributes) == 0x60,
                  "SEntityClassDesc::classAttributes offset mismatch");
    static_assert(sizeof(IEntityClassRegistry::SEntityClassDesc) == 0x70,
                  "SEntityClassDesc size mismatch");
} // namespace SDK
