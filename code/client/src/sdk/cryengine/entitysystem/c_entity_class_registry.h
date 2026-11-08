#pragma once

#include "i_entity_system.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete registry used to resolve entity classes before spawning.
     *
     * The table at 0x184075988 contains exactly the twelve
     * IEntityClassRegistry entries and adds no virtual methods. Its owning
     * CEntitySystem path allocates exactly 0x58 bytes for the registry.
     */
    class CEntityClassRegistry : public IEntityClassRegistry {
      public:
        ~CEntityClassRegistry() override = default;

        static CEntityClassRegistry *GetInstance();

      private:
        void *m_pClassTreeHead;                         // 0x08
        std::size_t m_classCount;                       // 0x10
        IEntityClass *m_pDefaultClass;                  // 0x18
        void *m_pEnvironment0032;                       // 0x20
        void *m_pIteratorNode;                          // 0x28
        IEntityClassRegistryListener **m_pListeners;    // 0x30
        IEntityClassRegistryListener **m_pListenersEnd; // 0x38
        IEntityClassRegistryListener **m_pListenersCapacity; // 0x40
        std::size_t m_listenerDispatchDepth;            // 0x48
        std::uint16_t m_listenerCleanupPending;         // 0x50
        std::byte m_padding0052[0x6];
    };

    inline CEntityClassRegistry *CEntityClassRegistry::GetInstance() {
        const auto entitySystem = IEntitySystem::GetInstance();
        if (entitySystem == nullptr) {
            return nullptr;
        }

        return static_cast<CEntityClassRegistry *>(entitySystem->GetClassRegistry());
    }

    static_assert(sizeof(CEntityClassRegistry) == 0x58, "CEntityClassRegistry size mismatch");
} // namespace SDK
