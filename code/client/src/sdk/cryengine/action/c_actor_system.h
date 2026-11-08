#pragma once

#include "../entitysystem/i_entity_pool_listener.h"
#include "../entitysystem/i_entity_system_sink.h"
#include "i_actor_system.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class IEntitySystem;
    class ISystem;

    /** MSVC std::unordered_map storage used for EntityId -> IActor entries. */
    struct ActorSystemActorMapLayout {
        float maxLoadFactor;          // 0x00
        std::uint32_t padding0004;
        void *listHead;               // 0x08
        std::size_t actorCount;       // 0x10
        void **bucketsBegin;          // 0x18
        void **bucketsEnd;            // 0x20
        void **bucketsCapacity;       // 0x28
        std::size_t bucketMask;       // 0x30
        std::size_t maximumBucket;    // 0x38
    };

    /** MSVC ordered tree storage used by CActorSystem. */
    struct ActorSystemTreeLayout {
        void *head;                   // 0x00
        std::size_t size;             // 0x08
    };

    template <typename T> struct ActorSystemPointerVectorLayout {
        T *begin;                     // 0x00
        T *end;                       // 0x08
        T *capacity;                  // 0x10

        std::size_t GetCount() const {
            return begin != nullptr
                       ? static_cast<std::size_t>(end - begin)
                       : 0;
        }
    };

    static_assert(sizeof(ActorSystemActorMapLayout) == 0x40,
                  "ActorSystemActorMapLayout size mismatch");
    static_assert(sizeof(ActorSystemTreeLayout) == 0x10,
                  "ActorSystemTreeLayout size mismatch");
    static_assert(sizeof(ActorSystemPointerVectorLayout<void *>) == 0x18,
                  "ActorSystemPointerVectorLayout size mismatch");

    /**
     * CryAction actor factory and live-actor registry.
     *
     * CActorSystem's three vtables are at 0x184048340 (IActorSystem),
     * 0x184048300 (IEntityPoolListener), and 0x1840482B8
     * (IEntitySystemSink). Its factory and scalar deleting destructor both
     * prove the complete 0xB8-byte allocation. CreateActor resolves the actor
     * class through IEntityClassRegistry, builds SEntitySpawnParams, calls
     * IEntitySystem::SpawnEntity and InitEntity, then looks the IActor up by
     * the spawned entity id.
     */
    class CActorSystem : public IActorSystem,
                         public IEntityPoolListener,
                         public IEntitySystemSink {
      public:
        /**
         * Concrete iterator returned by CreateActorIterator.
         *
         * RTTI preserves the exact nested source name. Its five-slot table is
         * precisely IActorIterator: deleting destructor, Count, Next, AddRef,
         * and Release. Construction and sized deletion prove a 0x20-byte
         * object. Release returns the final-reference iterator to the owning
         * CActorSystem's recycled-iterator vector at +0x88.
         */
        class CActorIterator : public IActorIterator {
          public:
            ~CActorIterator() override = default;

            CActorSystem *GetActorSystem() const { return m_pActorSystem; }
            std::int32_t GetReferenceCountRaw() const {
                return m_referenceCount;
            }

          private:
            CActorSystem *m_pActorSystem; // 0x08
            void *m_pCurrentActorNode;    // 0x10
            std::int32_t m_referenceCount; // 0x18
            std::uint32_t m_padding001C;
        };

        ~CActorSystem() override = default;

        static CActorSystem *GetInstance() {
            return static_cast<CActorSystem *>(IActorSystem::GetInstance());
        }

        ISystem *GetSystem() const { return m_pSystem; }
        IEntitySystem *GetEntitySystem() const { return m_pEntitySystem; }
        const ActorSystemActorMapLayout &GetActorMap() const { return m_actors; }
        const ActorSystemTreeLayout &GetActorClasses() const {
            return m_actorClasses;
        }
        const ActorSystemPointerVectorLayout<CActorIterator *> &
        GetRecycledActorIterators() const {
            return m_recycledActorIterators;
        }

      private:
        EntityId m_originalDemoSpectatorId; // 0x18
        EntityId m_currentDemoSpectatorId;  // 0x1C
        EntityId m_demoPlaybackMappedOriginalServerPlayerId; // 0x20
        std::uint32_t m_padding0024;
        ISystem *m_pSystem;                  // 0x28
        IEntitySystem *m_pEntitySystem;      // 0x30
        ActorSystemActorMapLayout m_actors;  // 0x38
        ActorSystemTreeLayout m_actorClasses; // 0x78
        ActorSystemPointerVectorLayout<CActorIterator *>
            m_recycledActorIterators; // 0x88
        ActorSystemTreeLayout m_actorParams; // 0xA0
        const char *m_actorParamsPath;        // 0xB0, CryStringT<char> payload
    };

    static_assert(sizeof(CActorSystem::CActorIterator) == 0x20,
                  "CActorSystem::CActorIterator size mismatch");
    static_assert(sizeof(CActorSystem) == 0xB8, "CActorSystem size mismatch");
} // namespace SDK
