#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * An actor's alignment solvers: what turns a body to, and moves it onto,
     * a target while an animation plays -- combat closing on an opponent,
     * an interaction settling on its seat or door.
     *
     * The interface `C_AlignmentManager` carries at +0x10 (table
     * 0x183E7FBF0). It holds one solver per kind; installing a new one
     * replaces the old. The two installers are proven against the combat
     * alignment callers, which call them through this table and register the
     * solver only when the returned id is non-zero.
     */
    class I_AlignmentManager {
      public:
        virtual ~I_AlignmentManager() = default; // slot 0
        virtual void unk_fn0001()     = 0;
        virtual void unk_fn0002()     = 0;
        virtual void unk_fn0003()     = 0;
        virtual void unk_fn0004()     = 0;
        virtual void unk_fn0005()     = 0;

        /**
         * Builds a position solver from `request` and installs it, returning
         * its id, or 0 when none was installed. 0x180A72B70. The request is
         * not modeled: +0x08 a flag, +0x10 a name, +0x28 the target.
         */
        virtual std::uint32_t SetPositionSolver(const void *request) = 0; // slot 6, +0x30

        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;

        /** The rotation counterpart of `SetPositionSolver`. 0x180A728E4. */
        virtual std::uint32_t SetRotationSolver(const void *request) = 0; // slot 20, +0xA0
    };
} // namespace SDK::wh::entitymodule
