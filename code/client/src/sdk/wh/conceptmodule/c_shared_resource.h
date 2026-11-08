#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::conceptmodule {
    /**
     * Base of everything the concept graph addresses by id. Every node and
     * every port is one of these, which is why a port can be referred to from
     * a different node by nothing but a number.
     *
     * The id at 0x08 is **not** what makes the object findable. `AddRef`
     * overwrites it on the zero-to-one transition, and that call is what
     * enters the object into the concept resource registry -- so anything
     * built at runtime has to be AddRef'd before it is handed to the graph.
     */
    class C_SharedResource {
      public:
        virtual ~C_SharedResource() = default;

        std::uint32_t GetResourceId() const {
            return m_resourceId;
        }

        std::uint16_t GetReferenceCount() const {
            return m_referenceCount;
        }

        /** Registers the resource on its first reference and answers its registry id. */
        std::uint32_t AddRef();

        /** Unregisters and destroys the resource on its last release. Answers the count before the decrement. */
        std::int16_t Release();

      private:
        std::uint32_t m_resourceId;     // 0x08
        std::uint16_t m_referenceCount; // 0x0C
        std::uint16_t m_flags;          // 0x0E, bit 1 is set by C_AutoTriggerable

        static void AssertLayout() {
            static_assert(offsetof(C_SharedResource, m_resourceId) == 0x08, "C_SharedResource::m_resourceId offset mismatch");
            static_assert(offsetof(C_SharedResource, m_referenceCount) == 0x0C, "C_SharedResource::m_referenceCount offset mismatch");
            static_assert(offsetof(C_SharedResource, m_flags) == 0x0E, "C_SharedResource::m_flags offset mismatch");
        }
    };

    static_assert(sizeof(C_SharedResource) == 0x10, "C_SharedResource size mismatch");

    /**
     * How a node manager and a port link both name a resource: the registry
     * id plus the generation the registry held when the handle was taken.
     * Resolving one that does not match the registry's current generation
     * answers null, which is how the graph survives a resource being freed
     * while a stale handle is still around.
     */
    struct S_WeakResourceId {
        std::uint32_t resourceId; // 0x00
        std::uint8_t generation;  // 0x04
        std::byte m_padding0005[0x3];
    };

    static_assert(sizeof(S_WeakResourceId) == 0x8, "S_WeakResourceId size mismatch");

    /** The generation a weak handle has to carry to resolve to `resourceId`. */
    std::uint8_t GetResourceGeneration(std::uint32_t resourceId);

    /** Fills a weak handle that will resolve back to `resource` for as long as it lives. */
    S_WeakResourceId MakeWeakResourceId(const C_SharedResource *resource);

    /**
     * Replaces a game-owned vector of resource pointers, releasing what was
     * there and taking a reference on each new entry.
     *
     * The buffer comes from the game's allocator, which is what
     * `std::_Allocate<16>` reduces to below a page, so the game can free it
     * itself. A page or more is refused and the vector left untouched: above
     * that size `_Allocate` switches to a manually aligned path whose
     * bookkeeping `_Deallocate` would undo from an address never written.
     */
    bool AssignResourceVector(SDK::StdVector<C_SharedResource *> &vector, C_SharedResource *const *entries, std::size_t count);

    /**
     * Fills a game-owned vector of weak handles, one per resource.
     *
     * Not every eight-byte element in this graph is a pointer: `C_NodeManager`
     * and `C_Quest::m_objectives` hold handles, and writing pointers into one
     * reads the low half of an address as an id and resolves to null. Both are
     * eight bytes, which is why nothing catches it.
     *
     * A handle carries no reference, so what it names is kept alive elsewhere.
     */
    bool AssignWeakResourceVector(SDK::StdVector<S_WeakResourceId> &vector, C_SharedResource *const *entries, std::size_t count);

    /** The same for a vector of a concrete resource type; the handle is one qword either way. */
    template <typename Resource>
    bool AssignResourceVector(SDK::StdVector<Resource *> &vector, Resource *const *entries, std::size_t count) {
        return AssignResourceVector(reinterpret_cast<SDK::StdVector<C_SharedResource *> &>(vector), reinterpret_cast<C_SharedResource *const *>(entries), count);
    }
} // namespace SDK::wh::conceptmodule
