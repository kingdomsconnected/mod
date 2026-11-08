#pragma once

#include "../../types.h"
#include "../animation/i_character_instance.h"
#include "i_actor.h"

namespace SDK {
    class IEquipmentManager;
    class IItem;
    class IItemParamsNode;
    class IItemSystemListener;
    class IStatObj;

    enum class IItemSystemQuery : int {
        FindItemByName,
    };

    /**
     * IItemSystem, abstract vtable 0x184051FC8; CItemSystem's complete
     * implementation is at its secondary vtable 0x1840521B0.
     */
    class IItemSystem {
      public:
        virtual ~IItemSystem() = default;
        virtual void Reset() = 0;
        virtual void Reload() = 0;
        virtual void PreReload() = 0;
        virtual void PostReload() = 0;
        virtual void Scan(const char *folderName) = 0;
        virtual IItemParamsNode *CreateParams() = 0;
        virtual const IItemParamsNode *GetItemParams(const char *itemName) const = 0;
        virtual const char *GetItemParamsDescriptionFile(const char *itemName) const = 0;
        virtual int GetItemParamsCount() const = 0;
        virtual const char *GetItemParamName(int index) const = 0;
        virtual std::uint8_t GetItemPriority(const char *item) const = 0;
        virtual const char *GetItemCategory(const char *item) const = 0;
        virtual std::uint8_t GetItemUniqueId(const char *item) const = 0;
        virtual bool IsItemClass(const char *name) const = 0;
        virtual const char *GetFirstItemClass() = 0;
        virtual const char *GetNextItemClass() = 0;
        virtual void RegisterForCollection(EntityId itemId) = 0;
        virtual void UnregisterForCollection(EntityId itemId) = 0;
        virtual void AddItem(EntityId itemId, IItem *item) = 0;
        virtual void RemoveItem(EntityId itemId, const char *itemName = nullptr) = 0;
        virtual IItem *GetItem(EntityId itemId) const = 0;
        virtual void SetConfiguration(const char *name) = 0;
        virtual const char *GetConfiguration() const = 0;
        virtual void *Query(IItemSystemQuery query, const void *param = nullptr) = 0;
        virtual ICharacterInstance *GetCachedCharacter(const char *fileName) = 0;
        virtual IStatObj *GetCachedObject(const char *fileName) = 0;
        virtual void CacheObject(const char *fileName, bool useCgfStreaming) = 0;
        virtual void CacheGeometry(const IItemParamsNode *geometry) = 0;
        virtual void CacheItemGeometry(const char *className) = 0;
        virtual void ClearGeometryCache() = 0;
        virtual void CacheItemSound(const char *className) = 0;
        virtual void ClearSoundCache() = 0;
        virtual void Serialize(TSerialize serialize) = 0;
        virtual EntityId GiveItem(IActor *actor, const char *item, bool sound, bool select, bool keepHistory,
                                  const char *setup = nullptr, std::uint32_t entityFlags = 0) = 0;
        virtual void SetActorItem(IActor *actor, EntityId itemId, bool keepHistory = true) = 0;
        virtual void SetActorItem(IActor *actor, const char *name, bool keepHistory = true) = 0;
        virtual void DropActorItem(IActor *actor, EntityId itemId) = 0;
        virtual void SetActorAccessory(IActor *actor, EntityId itemId, bool keepHistory = true) = 0;
        virtual void DropActorAccessory(IActor *actor, EntityId itemId) = 0;
        virtual void RegisterListener(IItemSystemListener *listener) = 0;
        virtual void UnregisterListener(IItemSystemListener *listener) = 0;
        virtual IEquipmentManager *GetIEquipmentManager() = 0;
        virtual std::uint32_t GetItemSocketCount(const char *item) const = 0;
        virtual const char *GetItemSocketName(const char *item, int index) const = 0;
        virtual bool IsCompatible(const char *item, const char *attachment) const = 0;
        virtual bool GetItemSocketCompatibility(const char *item, const char *socket) const = 0;
        virtual bool CanSocketBeEmpty(const char *item, const char *socket) const = 0;
    };
} // namespace SDK
