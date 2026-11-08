#pragma once

#include "i_render_node.h"

namespace SDK {
    /**
     * Specialized render node for dynamic world decals.
     *
     * RTTI preserves IDecalRenderNode between CDecalRenderNode and
     * IRenderNode. KCD2's concrete table contains all 73 IRenderNode entries
     * followed by exactly one decal-specific cleanup operation.
     */
    class IDecalRenderNode : public IRenderNode {
      public:
        ~IDecalRenderNode() override = default;

        virtual void CleanUpOldDecals() = 0; // +0x248
    };

    static_assert(sizeof(IDecalRenderNode) == 0x50,
                  "IDecalRenderNode size mismatch");
} // namespace SDK
