#pragma once

#include "i_game_object_system.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct GameObjectSystemTreeLayout {
        void *head;       // 0x00
        std::size_t size; // 0x08
    };

    struct GameObjectExtensionEntryLayout {
        const char *name; // 0x00, CryStringT<char> payload
        std::uint32_t serializationPriority; // 0x08
        std::uint32_t padding000C;
        IGameObjectExtensionCreatorBase *creator; // 0x10
    };

    struct GameObjectExtensionVectorLayout {
        GameObjectExtensionEntryLayout *begin;    // 0x00
        GameObjectExtensionEntryLayout *end;      // 0x08
        GameObjectExtensionEntryLayout *capacity; // 0x10
    };

    struct GameObjectSpawnSerializerLayout {
        EntityId entityId; // 0x00
        std::uint32_t padding0004;
        TSerialize *serializer; // 0x08
    };

    struct GameObjectSpawnSerializerVectorLayout {
        GameObjectSpawnSerializerLayout *begin;    // 0x00
        GameObjectSpawnSerializerLayout *end;      // 0x08
        GameObjectSpawnSerializerLayout *capacity; // 0x10
    };

    struct GameObjectPointerVectorLayout {
        IGameObject **begin;    // 0x00
        IGameObject **end;      // 0x08
        IGameObject **capacity; // 0x10
    };

    struct GameObjectSystemSinkListLayout {
        void *head;       // 0x00
        std::size_t size; // 0x08
    };

    static_assert(sizeof(GameObjectSystemTreeLayout) == 0x10,
                  "GameObjectSystemTreeLayout size mismatch");
    static_assert(sizeof(GameObjectExtensionEntryLayout) == 0x18,
                  "GameObjectExtensionEntryLayout size mismatch");
    static_assert(sizeof(GameObjectExtensionVectorLayout) == 0x18,
                  "GameObjectExtensionVectorLayout size mismatch");
    static_assert(sizeof(GameObjectSpawnSerializerLayout) == 0x10,
                  "GameObjectSpawnSerializerLayout size mismatch");
    static_assert(sizeof(GameObjectSpawnSerializerVectorLayout) == 0x18,
                  "GameObjectSpawnSerializerVectorLayout size mismatch");
    static_assert(sizeof(GameObjectPointerVectorLayout) == 0x18,
                  "GameObjectPointerVectorLayout size mismatch");
    static_assert(sizeof(GameObjectSystemSinkListLayout) == 0x10,
                  "GameObjectSystemSinkListLayout size mismatch");

    /**
     * Concrete CryAction extension registry and game-object factory.
     *
     * The implementation table at 0x184047800 contains exactly the 21
     * IGameObjectSystem entries. Its default constructor and scalar deleting
     * destructor prove the complete 0x158-byte layout. RegisterExtension
     * inserts the name/id mapping and 0x18-byte creator record, imports its RMI
     * metadata, and optionally registers the supplied entity-class descriptor.
     */
    class CGameObjectSystem : public IGameObjectSystem {
      public:
        ~CGameObjectSystem() override = default;

        static CGameObjectSystem *GetInstance();

        const GameObjectExtensionEntryLayout *GetExtensionsBegin() const {
            return m_extensions.begin;
        }

        const GameObjectExtensionEntryLayout *GetExtensionsEnd() const {
            return m_extensions.end;
        }

        std::size_t GetRegisteredExtensionCount() const {
            if (!m_extensions.begin) {
                return 0;
            }
            return static_cast<std::size_t>(m_extensions.end - m_extensions.begin);
        }

        const GameObjectSpawnSerializerVectorLayout &GetSpawnSerializers() const {
            return m_spawnSerializers;
        }

      private:
        GameObjectSystemTreeLayout m_extensionIdsByName; // 0x08
        GameObjectExtensionVectorLayout m_extensions;    // 0x18
        bool m_state0030;                                // 0x30
        std::byte m_padding0031[0x7];
        std::byte m_rmiData0038[0x48];                   // 0x38
        void *m_serverProtocolDefinitionVtable;          // 0x80
        void *m_clientProtocolDefinitionVtable;          // 0x88
        GameObjectPointerVectorLayout m_postUpdateObjects; // 0x90
        std::uint64_t m_state00A8;                       // 0xA8
        GameObjectSpawnSerializerVectorLayout m_spawnSerializers; // 0xB0
        GameObjectSystemTreeLayout m_schedulingProfiles; // 0xC8
        std::uint64_t m_defaultSchedulingProfile;        // 0xD8
        GameObjectSystemTreeLayout m_eventIdsByName;     // 0xE0
        GameObjectSystemTreeLayout m_eventNamesById;     // 0xF0
        GameObjectSystemSinkListLayout m_sinks;          // 0x100
        GameObjectPointerVectorLayout m_pendingPostUpdateObjects; // 0x110
        std::byte m_state0128[0x18];                     // 0x128
        const char **m_serializationOrderBegin;          // 0x140
        const char **m_serializationOrderEnd;            // 0x148
        const char **m_serializationOrderCapacity;       // 0x150
    };

    static_assert(sizeof(CGameObjectSystem) == 0x158,
                  "CGameObjectSystem size mismatch");
} // namespace SDK
