#include "c_shared_resource.h"

#include "i_resource_registry.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::conceptmodule {
    std::uint32_t C_SharedResource::AddRef() {
        return hook::this_call<std::uint32_t>(gPatterns.C_SharedResource__AddRef, this);
    }

    std::int16_t C_SharedResource::Release() {
        return hook::this_call<std::int16_t>(gPatterns.C_SharedResource__Release, this);
    }

    I_ResourceRegistry *GetResourceRegistry() {
        return hook::call<I_ResourceRegistry *>(gPatterns.ConceptResourceRegistry__Get);
    }

    std::uint8_t GetResourceGeneration(std::uint32_t resourceId) {
        return GetResourceRegistry()->GetGeneration(resourceId);
    }

    S_WeakResourceId MakeWeakResourceId(const C_SharedResource *resource) {
        S_WeakResourceId handle {};
        handle.resourceId = resource->GetResourceId();
        handle.generation = GetResourceGeneration(handle.resourceId);
        return handle;
    }

    bool AssignWeakResourceVector(SDK::StdVector<S_WeakResourceId> &vector, C_SharedResource *const *entries, std::size_t count) {
        const std::size_t bytes = count * sizeof(S_WeakResourceId);
        if (bytes >= 0x1000) {
            return false;
        }

        S_WeakResourceId *buffer = nullptr;
        if (count != 0) {
            buffer = hook::call<S_WeakResourceId *>(gPatterns.CryActionAllocate, bytes);
            for (std::size_t index = 0; index < count; ++index) {
                buffer[index] = entries[index] != nullptr ? MakeWeakResourceId(entries[index]) : S_WeakResourceId {};
            }
        }

        // Nothing is released: the handles that were here owned nothing.
        if (vector.first != nullptr) {
            hook::call<void>(gPatterns.FreeActionStorage, vector.first);
        }

        vector.first       = buffer;
        vector.last        = buffer != nullptr ? buffer + count : nullptr;
        vector.capacityEnd = vector.last;
        return true;
    }

    bool AssignResourceVector(SDK::StdVector<C_SharedResource *> &vector, C_SharedResource *const *entries, std::size_t count) {
        const std::size_t bytes = count * sizeof(C_SharedResource *);

        // Refused before anything is touched, so the caller is left with the
        // list it already had rather than an emptied one.
        if (bytes >= 0x1000) {
            return false;
        }

        // The new entries are claimed before the old ones are let go, because
        // the two lists usually overlap: a caller that appends to what it was
        // handed would otherwise release a resource to zero and watch it be
        // destroyed a line before it claimed it again.
        C_SharedResource **buffer = nullptr;
        if (count != 0) {
            buffer = hook::call<C_SharedResource **>(gPatterns.CryActionAllocate, bytes);
            for (std::size_t index = 0; index < count; ++index) {
                buffer[index] = entries[index];
                if (entries[index] != nullptr) {
                    entries[index]->AddRef();
                }
            }
        }

        for (C_SharedResource **slot = vector.first; slot != vector.last; ++slot) {
            if (*slot != nullptr) {
                (*slot)->Release();
            }
        }

        if (vector.first != nullptr) {
            hook::call<void>(gPatterns.FreeActionStorage, vector.first);
        }

        vector.first       = buffer;
        vector.last        = buffer != nullptr ? buffer + count : nullptr;
        vector.capacityEnd = vector.last;
        return true;
    }
} // namespace SDK::wh::conceptmodule
