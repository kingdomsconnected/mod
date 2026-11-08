#pragma once

#include "../../patterns.h"
#include "../../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One point of a cart path, 64 bytes, stored by value in C_CartPath's node
     * vector.
     *
     * The layout is `MakeCartPathNode` (0x1828F078C), which is how the engine
     * builds every node it appends: the point, both links unset, the corner
     * fillet zeroed and its tangent point on the node itself. `AppendNode`
     * fills the links, the running distance and the fillet of the node before
     * it, so a node is only ever made here and handed over -- never edited.
     */
    struct S_CartPathNode {
        Vec3 position;             // 0x00
        std::int32_t previous;     // 0x0C, -1 until linked
        std::int32_t next;         // 0x10, -1 until linked
        float distance;            // 0x14, path distance of this node, written on append
        float filletRadius;        // 0x18, the corner's rounding, min(path radius, half a segment)
        float filletTangent;       // 0x1C, how far back along each segment the rounding starts
        float filletAngle;         // 0x20
        float segmentLength;       // 0x24, straight run to the next node, less the fillet
        Vec3 filletStart;          // 0x28, the point the rounding leaves the straight at; the node itself when unrounded
        Vec3 filletAxis;           // 0x34, the rounding's turn axis; zero when unrounded

        /**
         * A fresh node at `position`, as the engine's builder makes them.
         * Through the native rather than written out, so the fields the
         * appender reads are whatever the game itself starts them at.
         */
        static S_CartPathNode At(const Vec3 &position) {
            S_CartPathNode node {};
            hook::call<S_CartPathNode *>(gPatterns.MakeCartPathNode, &node, &position);
            return node;
        }

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_CartPathNode, previous) == 0x0C, "S_CartPathNode::previous offset mismatch");
            static_assert(offsetof(S_CartPathNode, next) == 0x10, "S_CartPathNode::next offset mismatch");
            static_assert(offsetof(S_CartPathNode, distance) == 0x14, "S_CartPathNode::distance offset mismatch");
            static_assert(offsetof(S_CartPathNode, filletRadius) == 0x18, "S_CartPathNode::filletRadius offset mismatch");
            static_assert(offsetof(S_CartPathNode, segmentLength) == 0x24, "S_CartPathNode::segmentLength offset mismatch");
            static_assert(offsetof(S_CartPathNode, filletStart) == 0x28, "S_CartPathNode::filletStart offset mismatch");
            static_assert(offsetof(S_CartPathNode, filletAxis) == 0x34, "S_CartPathNode::filletAxis offset mismatch");
        }
    };

    static_assert(sizeof(S_CartPathNode) == 0x40, "S_CartPathNode size mismatch");
} // namespace SDK::wh::entitymodule
