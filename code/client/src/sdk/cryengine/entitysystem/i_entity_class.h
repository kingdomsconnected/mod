#pragma once

#include "../../types.h"

namespace SDK {
    class ICrySizer;
    class IEntity;
    class IEntityAttributeArray;
    class IEntityEventHandler;
    class IEntityPropertyHandler;
    class IEntityProxy;
    class IEntityScript;
    class IEntityScriptFileHandler;
    class IScriptTable;
    struct SEntitySpawnParams;

    struct IEntityProxyPtr {
        IEntityProxy *object;
        void *referenceCount;
    };

    struct SEditorClassInfo {
        const char *icon;
        const char *helper;
        const char *category;
    };

    /**
     * IEntityClass, vtable 0x184075850; CEntityClass's implementation is at
     * 0x183B282A0. KCD2 adds GetId as the final slot.
     */
    class IEntityClass {
      public:
        enum class EventValueType : int {
            Int,
            Float,
            Bool,
            Vector,
            Entity,
            String,
        };

        struct SEventInfo {
            const char *name;
            EventValueType type;
            bool output;
        };

        using UserProxyCreateFunc = IEntityProxyPtr (*)(IEntity *entity, SEntitySpawnParams &params, void *userData);

        virtual ~IEntityClass() = default; // +0x00
        virtual void Release() = 0;
        virtual const char *GetName() const = 0;
        virtual std::uint32_t GetFlags() const = 0;
        virtual void SetFlags(std::uint32_t flags) = 0;
        virtual const char *GetScriptFile() const = 0;
        virtual IEntityScript *GetIEntityScript() const = 0;
        virtual IScriptTable *GetScriptTable() const = 0;
        virtual IEntityPropertyHandler *GetPropertyHandler() const = 0;
        virtual IEntityEventHandler *GetEventHandler() const = 0;
        virtual IEntityScriptFileHandler *GetScriptFileHandler() const = 0;
        virtual const SEditorClassInfo &GetEditorClassInfo() const = 0;
        virtual void SetEditorClassInfo(const SEditorClassInfo &editorClassInfo) = 0;
        virtual bool LoadScript(bool forceReload) = 0;
        virtual UserProxyCreateFunc GetUserProxyCreateFunc() const = 0;
        virtual void *GetUserProxyData() const = 0;
        virtual int GetEventCount() = 0;
        virtual SEventInfo GetEventInfo(int index) = 0;
        virtual bool FindEventInfo(const char *eventName, SEventInfo &eventInfo) = 0;
        virtual IEntityAttributeArray &GetClassAttributes() = 0;
        virtual const IEntityAttributeArray &GetClassAttributes() const = 0;
        virtual IEntityAttributeArray &GetEntityAttributes() = 0;
        virtual const IEntityAttributeArray &GetEntityAttributes() const = 0;
        virtual void GetMemoryUsage(ICrySizer *sizer) const = 0;
        virtual std::uint32_t GetId() const = 0;
    };

    static_assert(sizeof(IEntityProxyPtr) == 0x10, "IEntityProxyPtr size mismatch");
    static_assert(sizeof(SEditorClassInfo) == 0x18, "SEditorClassInfo size mismatch");
    static_assert(sizeof(IEntityClass::SEventInfo) == 0x10, "IEntityClass::SEventInfo size mismatch");
} // namespace SDK
