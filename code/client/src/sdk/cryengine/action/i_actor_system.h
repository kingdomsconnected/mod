#pragma once

#include "i_actor.h"

namespace SDK {
    class IEntityClass;
    class IItemParamsNode;

    /** Native class names registered for IActorSystem::CreateActor. */
    namespace ActorClassNames {
        inline constexpr const char *NullAI = "NullAI";
        inline constexpr const char *Player = "Player";
        inline constexpr const char *PlayerFemale = "PlayerFemale";
        inline constexpr const char *NPC = "NPC";
        inline constexpr const char *NPCNonAI = "NPC_NAI";
        inline constexpr const char *NPCFemale = "NPC_Female";
        inline constexpr const char *DummyTarget = "DummyTarget";
        inline constexpr const char *Horse = "Horse";
        inline constexpr const char *InventoryDummyHorse =
            "InventoryDummyHorse";
        inline constexpr const char *Dog = "Dog";
        inline constexpr const char *Wolf = "Wolf";
        inline constexpr const char *WildDog = "WildDog";
    } // namespace ActorClassNames

    class IActorIterator {
      public:
        virtual ~IActorIterator() = default;
        virtual std::size_t Count() = 0;
        virtual IActor *Next() = 0;
        virtual void AddRef() = 0;
        virtual void Release() = 0;
    };

    class IActorIteratorPtr {
      public:
        IActorIteratorPtr(): pIterator(nullptr) {}

        explicit IActorIteratorPtr(IActorIterator *iterator): pIterator(iterator) {
            if (pIterator) {
                pIterator->AddRef();
            }
        }

        IActorIteratorPtr(const IActorIteratorPtr &other): IActorIteratorPtr(other.pIterator) {}

        ~IActorIteratorPtr() {
            if (pIterator) {
                pIterator->Release();
            }
        }

        IActorIterator *operator->() const { return pIterator; }
        explicit operator bool() const { return pIterator != nullptr; }

      private:
        IActorIterator *pIterator;
    };

    /**
     * IActorSystem, vtable 0x184048218; CActorSystem's implementation is at
     * 0x184048340. All 19 slots are mapped in their KCD2 order.
     */
    class IActorSystem {
      public:
        virtual ~IActorSystem() = default; // +0x00
        virtual void Reset() = 0;
        virtual void Reload() = 0;
        virtual IActor *GetActor(EntityId entityId) = 0;
        virtual IActor *GetActorByChannelId(ChannelId channelId) = 0;
        /**
         * Creates an actor using one of the native ActorClassNames strings.
         * The concrete implementation resolves the class, spawns and
         * initializes its entity, then returns the registered actor extension.
         */
        virtual IActor *CreateActor(ChannelId channelId, const char *name, const char *actorClass, const Vec3 &pos, const Quat &rot, const Vec3 &scale, EntityId id = 0) = 0;
        virtual int GetActorCount() const = 0;
        virtual IActorIteratorPtr CreateActorIterator() = 0;
        virtual void SetDemoPlaybackMappedOriginalServerPlayer(EntityId id) = 0;
        virtual EntityId GetDemoPlaybackMappedOriginalServerPlayer() const = 0;
        virtual void SwitchDemoSpectator(EntityId id = 0) = 0;
        virtual IActor *GetCurrentDemoSpectator() = 0;
        virtual IActor *GetOriginalDemoSpectator() = 0;
        virtual void AddActor(EntityId entityId, IActor *actor) = 0;
        virtual void RemoveActor(EntityId entityId) = 0;
        virtual void Scan(const char *folderName) = 0;
        virtual bool ScanXML(const XmlNodeRef &root, const char *xmlFile) = 0;
        virtual const IItemParamsNode *GetActorParams(const char *actorClass) const = 0;
        virtual bool IsActorClass(IEntityClass *entityClass) const = 0;

        /** Returns CryAction's process-wide actor system, when initialized. */
        static IActorSystem *GetInstance();
    };

    static_assert(sizeof(IActorIteratorPtr) == 0x8, "IActorIteratorPtr size mismatch");
} // namespace SDK
