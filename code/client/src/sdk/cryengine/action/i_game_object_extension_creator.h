#pragma once

#include <cstddef>

namespace SDK {
    class IGameObjectExtension;

    /** CryAction's shared game-object-extension handle. */
    struct IGameObjectExtensionPtr {
        IGameObjectExtension *object;
        void *referenceCount;
    };

    /**
     * Factory contract consumed by IGameObjectSystem::RegisterExtension.
     *
     * IGameObjectExtensionCreatorBase's abstract table at 0x183E916A0 has
     * exactly three entries. Independent CGameObjectExtensionCreator<T> and
     * CActorCreator<T> implementations prove the two non-destructor contracts:
     * construction returns the native shared extension handle, while the final
     * operation publishes the implementation's static RMI table and count.
     */
    class IGameObjectExtensionCreatorBase {
      public:
        virtual ~IGameObjectExtensionCreatorBase() = default; // +0x00
        virtual IGameObjectExtensionPtr Create() = 0;         // +0x08
        virtual void GetGameObjectExtensionRMIData(void **rmiData,
                                                   std::size_t *count) = 0; // +0x10
    };

    static_assert(sizeof(IGameObjectExtensionPtr) == 0x10,
                  "IGameObjectExtensionPtr size mismatch");
    static_assert(sizeof(IGameObjectExtensionCreatorBase) == 0x8,
                  "IGameObjectExtensionCreatorBase size mismatch");
} // namespace SDK
