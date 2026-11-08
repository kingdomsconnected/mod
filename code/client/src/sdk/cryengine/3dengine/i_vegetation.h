#pragma once

#include "i_render_node.h"

#include <cstddef>

namespace SDK {
    /**
     * Vegetation render-node specialization returned by terrain spawning.
     *
     * RTTI proves IVegetation derives from IRenderNode. CVegetation's table
     * contains the 73 inherited entries and one final vegetation operation;
     * no independent IVegetation storage is constructed before the concrete
     * class's Cry3DEngineBase subobject at +0x50.
     */
    class IVegetation : public IRenderNode {
      public:
        ~IVegetation() override = default;

        virtual float GetScale() const = 0; // final IVegetation slot, +0x248
    };

    static_assert(sizeof(IVegetation) == 0x50,
                  "IVegetation size mismatch");
} // namespace SDK
