#pragma once

#include "i_render_node.h"

#include <cstddef>

namespace SDK {
    /**
     * Static-geometry render-node specialization used by CBrush props.
     *
     * RTTI proves IBrush derives from IRenderNode. CBrush's table contains
     * the 73 inherited entries followed by exactly five brush operations;
     * the concrete class does not add any further virtual slots. The first
     * three are proven by their flag writes and transform math. Slot 76 owns a
     * non-trivial compound sub-object mask whose exact public ABI is not yet
     * safe to expose, and slot 77 remains semantically unknown.
     */
    class IBrush : public IRenderNode {
      public:
        ~IBrush() override = default;

        virtual void SetDrawLast(bool enable) = 0; // +0x248
        virtual void DisablePhysicalization(bool disable) = 0; // +0x250
        virtual float GetScale() const = 0; // +0x258
        virtual void unk_fn0076() = 0;
        virtual void unk_fn0077() = 0;
    };

    static_assert(sizeof(IBrush) == 0x50,
                  "IBrush size mismatch");
} // namespace SDK
