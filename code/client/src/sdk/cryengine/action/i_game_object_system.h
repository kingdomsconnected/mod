#pragma once

#include "../../types.h"
#include "../entitysystem/i_entity.h"
#include "../entitysystem/i_entity_class_registry.h"
#include "i_game_object_extension_creator.h"
#include <cstddef>

namespace SDK {
    class IGameObject;
    class IGameObjectExtension;
    class IGameObjectSystemSink;
    class IEntityProxy;
    class IProtocolBuilder;
    struct SGameObjectEvent;

    /**
     * IGameObjectSystem, vtable 0x184047750; CGameObjectSystem's complete
     * implementation is at 0x184047800.
     */
    class IGameObjectSystem {
      public:
        /** Native CreateActor stack payload at 0x180B86120. */
        struct SEntitySpawnParamsForGameObjectWithPreactivatedExtension {
            bool (*hookFunction)(SDK::IEntity *, SDK::IGameObject *, void *) = nullptr;
            void *pUserData = nullptr;

          private:
            std::uint32_t m_type = 0;
            std::byte m_opaque0014[4] {};

            static void AssertLayout() {
                static_assert(offsetof(SEntitySpawnParamsForGameObjectWithPreactivatedExtension, hookFunction) == 0x00);
                static_assert(offsetof(SEntitySpawnParamsForGameObjectWithPreactivatedExtension, pUserData) == 0x08);
                static_assert(offsetof(SEntitySpawnParamsForGameObjectWithPreactivatedExtension, m_type) == 0x10);
            }
        };
        static_assert(sizeof(SEntitySpawnParamsForGameObjectWithPreactivatedExtension) == 0x18);
        using ExtensionID = std::uint16_t;
        static constexpr ExtensionID InvalidExtensionID = 0xFFFF;

        virtual ~IGameObjectSystem() = default; // +0x000
        virtual ExtensionID GetID(const char *name) = 0;
        virtual const char *GetName(ExtensionID id) = 0;
        virtual std::uint32_t GetExtensionSerializationPriority(ExtensionID id) = 0;
        virtual IGameObjectExtensionPtr Instantiate(ExtensionID id, IGameObject *gameObject) = 0;
        virtual void BroadcastEvent(const SGameObjectEvent &event) = 0;
        virtual void RegisterEvent(std::uint32_t id, const char *name) = 0;
        virtual std::uint32_t GetEventID(const char *name) = 0;
        virtual const char *GetEventName(std::uint32_t id) = 0;
        virtual IGameObject *CreateGameObjectForEntity(EntityId entityId) = 0;
        virtual IEntityProxyPtr CreateGameObjectEntityProxy(IEntity &entity, IGameObject **gameObject = nullptr) = 0;
        virtual void RegisterExtension(const char *name, IGameObjectExtensionCreatorBase *creator,
                                       IEntityClassRegistry::SEntityClassDesc *entityClass) = 0;
        virtual void RegisterSchedulingProfile(const char *entityClassName, const char *normalPolicy,
                                               const char *ownedPolicy) = 0;
        virtual void DefineProtocol(bool server, IProtocolBuilder *builder) = 0;
        virtual void PostUpdate(float frameTime) = 0;
        virtual void SetPostUpdate(IGameObject *gameObject, bool enable) = 0;
        virtual void Reset() = 0;
        virtual void SetSpawnSerializerForEntity(EntityId entityId, TSerialize *serializer) = 0;
        virtual void ClearSpawnSerializerForEntity(EntityId entityId) = 0;
        virtual void AddSink(IGameObjectSystemSink *sink) = 0;
        virtual void RemoveSink(IGameObjectSystemSink *sink) = 0;
    };

} // namespace SDK
