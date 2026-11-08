#pragma once

#include <cstddef>

namespace SDK::rttr {
    /**
     * Storage boundary of the reflection library's `variant`: sixteen bytes of
     * small-object storage and the policy pointer, which is both the type tag
     * and what performs every operation on the value.
     *
     * Nothing here constructs or destroys one. A variant frees its storage by
     * calling its own policy, which lives in the game, so one is only ever read
     * in place or handed to a native that takes it.
     */
    struct Variant {
        std::byte storage[0x10];
        void *pPolicy;
    };

    static_assert(sizeof(Variant) == 0x18, "rttr::Variant size mismatch");
} // namespace SDK::rttr
