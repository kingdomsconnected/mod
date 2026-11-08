#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Output of `ClassifyFollowTarget`. Mode 0/1/2 is the followable side
     * effect the target carries: centre, left or right.
     */
    struct S_FollowTargetMode {
        std::uint8_t mode;
        bool valid;
    };
    static_assert(sizeof(S_FollowTargetMode) == 2);
    static_assert(offsetof(S_FollowTargetMode, mode) == 0);
    static_assert(offsetof(S_FollowTargetMode, valid) == 1);
} // namespace SDK::wh::entitymodule
