#pragma once

#include "c_shared_resource.h"

#include <cstdint>

namespace SDK::wh::conceptmodule {
    /**
     * The registry every concept resource enters itself into, and the only
     * thing that can turn a weak handle back into an object.
     *
     * There is no virtual destructor, so slot 0 is `Register` and the indices
     * below are the ones the game's own call sites use. Slot 2 is the resolve,
     * left unnamed because it answers through a hidden return pointer and the
     * game resolves handles for itself; what the mod produces is a handle the
     * game will accept.
     */
    class I_ResourceRegistry {
      public:
        /** Enters a resource and answers the id it is now known by. */
        virtual std::uint32_t Register(C_SharedResource *resource) = 0;

        /** Drops the entry and advances the slot's generation. */
        virtual void Unregister(std::uint32_t resourceId) = 0;

        virtual void ResourceRegistryOpaque02() = 0;

        /** Reserves an id without entering anything under it. */
        virtual std::uint32_t AllocateId() = 0;

        /** The generation a handle has to carry to still resolve. */
        virtual std::uint8_t GetGeneration(std::uint32_t resourceId) = 0;
    };

    /** The process-wide registry, reached through the concept module. */
    I_ResourceRegistry *GetResourceRegistry();
} // namespace SDK::wh::conceptmodule
