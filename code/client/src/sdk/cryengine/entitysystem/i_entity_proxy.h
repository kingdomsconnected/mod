#pragma once

#include "../../types.h"
#include "../scriptsystem/i_script_table.h"
#include "i_entity.h"

namespace SDK {
    class ICrySizer;
    struct SEntityEvent;
    struct SEntitySpawnParams;
    struct SEntityUpdateContext;

    using ComponentEventPriority = int;

    enum class EEntityProxy : int {
        /** The entity's `CRenderProxy`; its `GetType` (0x18066CD10) is `xor eax, eax; ret`. */
        Render = 0,
        Script = 2,
        /** The entity's area volume; see `IEntityAreaProxy`. `CAreaProxy::GetType` is `mov eax, 5; ret`, so this is read rather than counted. */
        Area = 5,
        /** The entity's audio component; see `IEntityAudioProxy`. */
        Audio = 3,
        /** The entity's `CGameObject`, and through it every game-object extension. `CGameObject::GetType` (0x181A7DFD0) is `mov eax, 11h; ret`. */
        User = 17,
        /**
         * Warhorse's lightweight game object, for entities that need no
         * `CGameObject`: `I_GameObjectLight`, `C_TagPoint`, `C_ItemSlot`,
         * `C_ParticleEffect` and `C_Shop` all share the `GetType` at
         * 0x181A83AC0, `mov eax, 12h; ret`. The entity constructor files a
         * class-created proxy under the id its `GetType` returns, so a class
         * whose creator makes one of these has nothing at `User`.
         */
        GameObjectLight = 18,
    };

    /**
     * KCD2's IComponent prefix. Its abstract table has two additions relative
     * to the public legacy interface; unresolved additions stay opaque.
     */
    class IComponent {
      public:
        virtual ~IComponent() = default; // +0x000
        virtual void ProcessEvent(SEntityEvent &event) = 0;
        virtual void Opaque02() = 0;
        virtual ComponentEventPriority GetEventPriority(int eventId) const = 0;
        virtual void Opaque04() = 0;
        virtual void Opaque05() = 0;
        virtual void GetMemoryUsage(ICrySizer *sizer) const = 0;
    };

    /**
     * IEntityProxy's exact 22-slot interface prefix.
     *
     * CGameObject's primary table at 0x183A5DC08 appends three concrete
     * operations after this interface. The independent I_GameObjectLight
     * implementation table terminates after slot 21, proving those additions
     * do not belong to IEntityProxy.
     */
    class IEntityProxy : public IComponent {
      public:
        virtual EEntityProxy GetType() = 0; // +0x038
        virtual bool Init(IEntity *entity, SEntitySpawnParams &params) = 0;
        virtual void Reload(IEntity *entity, SEntitySpawnParams &params) = 0;
        virtual void Done() = 0;
        virtual void Release() = 0;
        virtual void Update(SEntityUpdateContext &context) = 0;
        virtual void SerializeXML(XmlNodeRef &entityNode, bool loading) = 0;
        virtual void Serialize(TSerialize serialize) = 0;
        virtual bool NeedSerialize() = 0;
        virtual bool GetSignature(TSerialize signature) = 0;
        virtual void Opaque17() = 0;
        /** Script proxy only: per-entity Lua state/self delegated to the class table. */
        virtual IScriptTable *GetScriptTable() const = 0; // +0x090
        virtual void Opaque19() = 0;
        virtual void Opaque20() = 0;
        virtual void Opaque21() = 0;
    };
} // namespace SDK
