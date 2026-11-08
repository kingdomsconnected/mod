#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "s_cart_path_node.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The polyline a cart travels: its nodes, and how its corners are rounded.
     *
     * RTTI names it through `std::_Ref_count_obj2<wh::entitymodule::C_CartPath>`,
     * the control block `MakeSharedCartPath` (0x1828EF990) builds it in: a
     * cart holds it by `std::shared_ptr` at C_CartFollowerModel+0xD8, and the
     * path manager caches one per path entity so carts on the same road share
     * it. 0x28 bytes.
     *
     * The node vector is the game's own `std::vector` on the game's heap, so
     * it is read in place and grown only through `AppendNode`, never touched
     * from here -- a reallocation made by this module would free through the
     * wrong allocator.
     *
     * A level cart's nodes come from the `C_PredefinedPath` its path entity
     * names (`BuildFromPathEntity`, 0x1828F974C). Nothing about the path
     * requires that: `AppendNode` is the whole of what the builder does, one
     * point at a time, and it rounds only the corner the new point makes.
     */
    class C_CartPath {
      public:
        std::size_t GetNodeCount() const {
            return static_cast<std::size_t>(m_pNodesEnd - m_pNodes);
        }

        const S_CartPathNode &GetNode(std::size_t index) const {
            return m_pNodes[index];
        }

        /** The running path distance at the last node: how long the path is. */
        float GetLength() const {
            return m_pNodes != m_pNodesEnd ? m_pNodesEnd[-1].distance : 0.0F;
        }

        /** The entity the path was built for; the path manager's key. */
        EntityId GetPathEntityId() const {
            return m_pathEntityId;
        }

        void SetPathEntityId(EntityId pathEntityId) {
            m_pathEntityId = pathEntityId;
        }

        /**
         * Pushes one node, links it to the last, and rounds the corner the
         * previous node now makes. O(1): nothing before that corner is read
         * or written again, which is what makes growing a live path safe for
         * a cursor already travelling on it.
         */
        void AppendNode(const S_CartPathNode &node) {
            hook::this_call<void>(gPatterns.C_CartPath__AppendNode, this, &node);
        }

        /**
         * Drops every node from `count` on, so that the next `AppendNode`
         * continues from node `count - 1` as if the dropped ones had never
         * been laid. The caller appends at least one node straight after,
         * under the same lock: until it does, the last kept node still links
         * to the dropped one and carries the rounding of a corner it no longer
         * makes.
         *
         * Exact because `AppendNode` (0x1828F0E98) reads nothing past the two
         * nodes before the one it pushes, and rewrites every field of theirs
         * that the dropped node set: the last kept node's fillet, distance
         * and segment length, and the segment length of the one before it.
         * Its `next` link is left as is, and already names the index the next
         * node lands at. The nodes are plain data, so the vector shrinks by
         * its end pointer alone, as `std::vector::resize` shrinks one, and no
         * memory changes hands between the game's allocator and this module's.
         *
         * Only for nodes no cursor has reached: a cursor point records the node
         * it is past, and its segment's far node is rounded again.
         */
        void TruncateTo(std::size_t count) {
            if (count < GetNodeCount()) {
                m_pNodesEnd = m_pNodes + count;
            }
        }

      private:
        S_CartPathNode *m_pNodes;         // 0x00, std::vector<S_CartPathNode> begin
        S_CartPathNode *m_pNodesEnd;      // 0x08
        S_CartPathNode *m_pNodesCapacity; // 0x10
        float m_cornerRadius;             // 0x18, 3.0 from MakeSharedCartPath
        float m_maxCornerTangent;         // 0x1C, 50.0: a corner sharper than this rounding allows is left square
        EntityId m_pathEntityId;          // 0x20
        std::byte m_padding0024[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_CartPath, m_cornerRadius) == 0x18, "C_CartPath::m_cornerRadius offset mismatch");
            static_assert(offsetof(C_CartPath, m_maxCornerTangent) == 0x1C, "C_CartPath::m_maxCornerTangent offset mismatch");
            static_assert(offsetof(C_CartPath, m_pathEntityId) == 0x20, "C_CartPath::m_pathEntityId offset mismatch");
        }
    };

    static_assert(sizeof(C_CartPath) == 0x28, "C_CartPath size mismatch");
} // namespace SDK::wh::entitymodule
