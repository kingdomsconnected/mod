#pragma once

#include <cstddef>

namespace SDK {
    /**
     * Empty common base used by Cry3DEngine implementation objects.
     *
     * RTTI emits this non-polymorphic base at class-dependent displacements,
     * including CVegetation+0x50. No constructor, destructor, vptr, or instance
     * state is emitted; MSVC applies empty-base optimization in derived types.
     */
    struct Cry3DEngineBase {};

    static_assert(sizeof(Cry3DEngineBase) == 0x1,
                  "Cry3DEngineBase size mismatch");
} // namespace SDK
