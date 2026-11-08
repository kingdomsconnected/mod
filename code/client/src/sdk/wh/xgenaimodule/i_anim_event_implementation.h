#pragma once

#include <cstddef>

namespace SDK::wh::xgenaimodule {
    /** Base contract for gameplay implementations of animation events. */
    class I_AnimEventImplementation {
      public:
        virtual ~I_AnimEventImplementation() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() {}
        virtual void unk_fn0005() {}
        virtual void unk_fn0006() {}
        virtual void unk_fn0007() {}
        virtual void unk_fn0008() {}
        virtual void unk_fn0009() {}
        virtual void unk_fn0010() {}
        virtual void unk_fn0011() {}
        virtual void unk_fn0012() {}
    };

    static_assert(sizeof(I_AnimEventImplementation) == 0x8,
                  "I_AnimEventImplementation size mismatch");
} // namespace SDK::wh::xgenaimodule
