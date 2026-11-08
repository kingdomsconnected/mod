#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Ground-alignment flags, including the extra KCD2 bit. */
    enum EGroundAlignment : std::uint8_t {
        eGA_Enable                     = 1 << 0,
        eGA_AllowWithNoCollision       = 1 << 1,
        eGA_AllowWhenHasGroundCollider = 1 << 2,
        eGA_PoseAlignerUseRootOffset   = 1 << 3,
        /** Warhorse's: C_Actor::ConfigureGroundAlignment (0x1809CECD0) sets it, C_Horse's (0x181361A30) clears it. */
        eGA_WarhorseBit5 = 1 << 5,
    };

    /** CryEngine `SGroundAlignmentParams`, returned by IAnimatedCharacter::GetGroundAlignmentParams. */
    struct SGroundAlignmentParams {
        float ikDisableDistanceSqr; // 0x00
        std::uint8_t flags;         // 0x04, EGroundAlignment

        static void AssertLayout() {
            static_assert(offsetof(SGroundAlignmentParams, flags) == 0x04, "SGroundAlignmentParams::flags offset mismatch");
        }
    };
} // namespace SDK
