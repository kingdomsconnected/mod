#pragma once

#include "../entitysystem/i_entity.h"
#include "../entitysystem/i_entity_proxy.h"
#include "i_game_object.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK {
    /** One sorted CGameObject extension record. */
    struct GameObjectExtensionLayout {
        std::uint64_t eventMask;                    // 0x00
        IGameObjectExtension *extension;            // 0x08
        void *sharedOwnership;                      // 0x10
        IGameObjectSystem::ExtensionID extensionId; // 0x18
        std::uint8_t acquisitionCount;              // 0x1A
        std::byte state001B[0xE];                   // 0x1B
        std::uint8_t flags;                         // 0x29
        std::byte state002A[0x6];                   // 0x2A
    };

    struct ConcreteGameObjectExtensionVectorLayout {
        GameObjectExtensionLayout *begin;    // 0x00
        GameObjectExtensionLayout *end;      // 0x08
        GameObjectExtensionLayout *capacity; // 0x10
    };

    static_assert(sizeof(GameObjectExtensionLayout) == 0x30,
                  "GameObjectExtensionLayout size mismatch");
    static_assert(sizeof(ConcreteGameObjectExtensionVectorLayout) == 0x18,
                  "ConcreteGameObjectExtensionVectorLayout size mismatch");

    /**
     * Concrete CryAction object attached to every spawned game entity.
     *
     * RTTI proves IEntityProxy at +0x00,
     * std::enable_shared_from_this<IComponent> at +0x08, and IGameObject at
     * +0x28. The secondary interface is represented as an explicit vtable
     * slot so the SDK can preserve the native displacement without inventing
     * an extra source-level base. The scalar deleting destructor passes 0xF8
     * to the allocator.
     *
     * Extensions are sorted 0x30-byte records. QueryExtension binary-searches
     * their 16-bit ids; event dispatch and channel initialization walk the
     * same vector. This is the attachment point for spawned player, NPC,
     * horse, animal, and other game-object extensions.
     */
    class CGameObject
        : public IEntityProxy,
          public std::enable_shared_from_this<IComponent> {
      public:
        ~CGameObject() override = default;

        /**
         * The concrete object behind an `IGameObject`: a subobject at +0x28, the
         * displacement `CGameObject::EnablePrePhysicsUpdate` (0x180DF0D38) itself
         * undoes before touching a field.
         */
        static CGameObject *FromIGameObject(IGameObject *object) {
            if (object == nullptr) {
                return nullptr;
            }
            return reinterpret_cast<CGameObject *>(reinterpret_cast<std::byte *>(object) - 0x28);
        }

        /**
         * The extension `entity`'s game object registered as `name`, or null
         * when the entity has no game object or no such extension.
         */
        static IGameObjectExtension *QueryExtension(IEntity *entity, const char *name) {
            auto *proxy = entity != nullptr ? entity->GetProxy(static_cast<int>(EEntityProxy::User)) : nullptr;
            if (proxy == nullptr) {
                return nullptr;
            }
            return static_cast<CGameObject *>(proxy)->GetIGameObject()->QueryExtensionByName(name);
        }

        IGameObject *GetIGameObject() {
            return reinterpret_cast<IGameObject *>(
                reinterpret_cast<std::byte *>(this) + 0x28);
        }

        const IGameObject *GetIGameObject() const {
            return reinterpret_cast<const IGameObject *>(
                reinterpret_cast<const std::byte *>(this) + 0x28);
        }

        EntityId GetEntityId() const { return m_entityId; }
        IEntity *GetEntity() const { return m_pEntity; }
        IActionListener *GetCapturedActionListener() const {
            return m_pActionListener;
        }
        IGameObjectView *GetCapturedView() const { return m_pView; }
        IGameObjectProfileManager *GetProfileManager() const {
            return m_pProfileManager;
        }
        void *GetUserData() const { return m_pUserData; }
        ChannelId GetChannelIdValue() const { return m_channelId; }
        int GetPredictionHandleValue() const { return m_predictionHandle; }
        EPrePhysicsUpdate GetPrePhysicsUpdateMode() const {
            return m_prePhysicsUpdateMode;
        }

        /** Whether pre-physics updates are live, not merely asked for (0x1807FF6D3). */
        bool IsPrePhysicsEnabled() const { return (m_flags00AC & 0x10) != 0; }

        /** What makes this object's AI count as activated; see `ShouldUpdateAI`. */
        EGameObjectAIActivationMode GetAIActivationMode() const {
            return static_cast<EGameObjectAIActivationMode>((m_flags00B0 >> 3) & 0x3);
        }
        EntityId GetNetworkParentId() const { return m_networkParentId; }

        const std::uint8_t *GetAspectProfiles() const {
            return m_aspectProfiles;
        }

        const ConcreteGameObjectExtensionVectorLayout &GetExtensions() const {
            return m_extensions;
        }

        std::size_t GetExtensionCount() const {
            if (!m_extensions.begin) {
                return 0;
            }
            return static_cast<std::size_t>(m_extensions.end - m_extensions.begin);
        }

        bool IsJustExchangingValue() const { return (m_flags00AC & 0x4) != 0; }

      private:
        std::byte m_state0018[0x10];                  // 0x18
        void *m_pIGameObjectVtable;                   // 0x28
        EntityId m_entityId;                          // 0x30
        std::uint32_t m_padding0034;
        void *m_state0038;                            // 0x38
        IEntity *m_pEntity;                           // 0x40
        IActionListener *m_pActionListener;           // 0x48
        IGameObjectView *m_pView;                     // 0x50
        IGameObjectProfileManager *m_pProfileManager; // 0x58
        std::uint8_t m_aspectProfiles[32];            // 0x60
        void *m_pUserData;                            // 0x80
        ConcreteGameObjectExtensionVectorLayout m_extensions; // 0x88
        ChannelId m_channelId;                        // 0xA0
        std::byte m_state00A2[0x2];
        std::int32_t m_state00A4;
        std::int32_t m_state00A8;
        std::uint32_t m_flags00AC;
        std::uint32_t m_flags00B0;
        std::byte m_state00B4[0x1C];                  // 0xB4
        float m_visibilityTimeout;                    // 0xD0
        std::byte m_state00D4[0xC];
        std::int32_t m_predictionHandle;              // 0xE0
        EPrePhysicsUpdate m_prePhysicsUpdateMode;     // 0xE4
        void *m_pDistanceChecker;                     // 0xE8
        std::byte m_state00F0[0x4];
        EntityId m_networkParentId;                   // 0xF4
    };

    static_assert(sizeof(CGameObject) == 0xF8,
                  "CGameObject size mismatch");
} // namespace SDK
