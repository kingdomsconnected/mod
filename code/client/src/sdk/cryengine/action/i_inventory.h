#pragma once

#include "../../types.h"
#include "i_actor.h"

namespace SDK {
    class IEntityClass;
    class IInventoryListener;

    /**
     * CryAction IInventory interface implemented by KCD2's CInventory.
     *
     * CInventory's vtable is at 0x18478DFB0. Slots 0-28 are inherited from
     * IGameObjectExtension, slots 29-86 retain the established CryAction
     * inventory contract, and KCD2 appends one unidentified slot at 87.
     */
    class IInventory: public IGameObjectExtension {
      public:
        enum EInventorySlots : int {
            eInventorySlot_Weapon = 0,
            eInventorySlot_Explosives,
            eInventorySlot_Grenades,
            eInventorySlot_Special,
            eInventorySlot_Last,
        };

        virtual ~IInventory()                                                                                 = default;
        virtual bool AddItem(EntityId id)                                                                     = 0; // +0x0E8
        virtual bool RemoveItem(EntityId id)                                                                  = 0;
        virtual void RemoveAllItems(bool forceClear = false)                                                  = 0;
        virtual bool AddAccessory(IEntityClass *itemClass)                                                    = 0;
        virtual void Destroy()                                                                                = 0;
        virtual void Clear(bool forceClear = false)                                                           = 0;
        virtual void RMIReqToServer_RemoveAllItems() const                                                    = 0;
        virtual void RMIReqToServer_AddItem(const char *itemClass) const                                      = 0;
        virtual void RMIReqToServer_RemoveItem(const char *itemClass) const                                   = 0;
        virtual void RMIReqToServer_SetAmmoCount(const char *ammoClass, int amount) const                     = 0;
        virtual void RMIReqToServer_AddEquipmentPack(const char *equipmentPack, bool add, bool primary) const = 0;
        virtual int GetCapacity() const                                                                       = 0;
        virtual int GetCount() const                                                                          = 0;
        virtual int GetCountOfClass(const char *className) const                                              = 0;
        virtual int GetCountOfCategory(const char *categoryName) const                                        = 0;
        virtual int GetCountOfUniqueId(std::uint8_t uniqueId) const                                           = 0;
        virtual int GetSlotCount(int slotId) const                                                            = 0;
        virtual EntityId GetItem(int slotId) const                                                            = 0;
        virtual const char *GetItemString(int slotId) const                                                   = 0;
        virtual EntityId GetItemByClass(IEntityClass *itemClass, IItem *ignoreItem = nullptr) const           = 0;
        virtual IItem *GetItemByName(const char *name) const                                                  = 0;
        virtual int GetAccessoryCount() const                                                                 = 0;
        virtual const char *GetAccessory(int slotId) const                                                    = 0;
        virtual const IEntityClass *GetAccessoryClass(int slotId) const                                       = 0;
        virtual bool HasAccessory(IEntityClass *itemClass) const                                              = 0;
        virtual int FindItem(EntityId itemId) const                                                           = 0;
        virtual int FindNext(IEntityClass *itemClass, const char *category, int firstSlot, bool wrap) const   = 0;
        virtual int FindPrev(IEntityClass *itemClass, const char *category, int firstSlot, bool wrap) const   = 0;
        virtual EntityId GetCurrentItem() const                                                               = 0;
        virtual EntityId GetHolsteredItem() const                                                             = 0;
        virtual void SetCurrentItem(EntityId itemId)                                                          = 0;
        virtual void SetHolsteredItem(EntityId itemId)                                                        = 0;
        virtual void SetLastItem(EntityId itemId)                                                             = 0;
        virtual EntityId GetLastItem() const                                                                  = 0;
        virtual EntityId GetLastSelectedInSlot(EInventorySlots slotId) const                                  = 0;
        virtual void HolsterItem(bool holster)                                                                = 0;
        virtual void SerializeInventoryForLevelChange(TSerialize serialize)                                   = 0;
        virtual bool IsSerializingForLevelChange() const                                                      = 0;
        virtual int GetAmmoTypesCount() const                                                                 = 0;
        virtual IEntityClass *GetAmmoType(int index) const                                                    = 0;
        virtual void SetAmmoCount(IEntityClass *ammoType, int count)                                          = 0;
        virtual int GetAmmoCount(IEntityClass *ammoType) const                                                = 0;
        virtual void SetAmmoCapacity(IEntityClass *ammoType, int maximum)                                     = 0;
        virtual int GetAmmoCapacity(IEntityClass *ammoType) const                                             = 0;
        virtual void ResetAmmo()                                                                              = 0;
        virtual void AddAmmoUser(IEntityClass *ammoType)                                                      = 0;
        virtual void RemoveAmmoUser(IEntityClass *ammoType)                                                   = 0;
        virtual int GetNumberOfUsersForAmmo(IEntityClass *ammoType) const                                     = 0;
        virtual IActor *GetActor()                                                                            = 0;
        virtual void SetInventorySlotCapacity(EInventorySlots slotId, unsigned int capacity)                  = 0;
        virtual void AssociateItemCategoryToSlot(const char *itemCategory, EInventorySlots slotId)            = 0;
        virtual bool IsAvailableSlotForItemClass(const char *itemClass) const                                 = 0;
        virtual bool IsAvailableSlotForItemCategory(const char *itemCategory) const                           = 0;
        virtual bool AreItemsInSameSlot(const char *firstItemClass, const char *secondItemClass) const        = 0;
        virtual EInventorySlots GetSlotForItemCategory(const char *itemCategory) const                        = 0;
        virtual void AddListener(IInventoryListener *listener)                                                = 0;
        virtual void RemoveListener(IInventoryListener *listener)                                             = 0;
        virtual void IgnoreNextClear()                                                                        = 0;
        virtual void unk_fn0087()                                                                             = 0; // +0x2B8
    };

    class IInventoryListener {
      public:
        virtual ~IInventoryListener()                                  = default;
        virtual void OnAddItem(EntityId entityId)                      = 0;
        virtual void OnSetAmmoCount(IEntityClass *ammoType, int count) = 0;
        virtual void OnAddAccessory(IEntityClass *accessoryClass)      = 0;
        virtual void OnClearInventory()                                = 0;
    };

    static_assert(sizeof(IInventory) == 0x40, "IInventory size mismatch");
} // namespace SDK
