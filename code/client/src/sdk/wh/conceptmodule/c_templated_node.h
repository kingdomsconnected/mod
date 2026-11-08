#pragma once

#include "c_node.h"

#include <cstddef>

namespace SDK::wh::conceptmodule {
    /**
     * A node whose ports are described by a reflected type rather than by the
     * graph data alone. It adds one field: the rttr type handle its
     * constructor installs.
     */
    class C_TemplatedNode: public C_Node {
      private:
        void *m_pReflectedType; // 0x40

        static void AssertLayout() {
            static_assert(offsetof(C_TemplatedNode, m_pReflectedType) == 0x40, "C_TemplatedNode::m_pReflectedType offset mismatch");
        }
    };

    static_assert(sizeof(C_TemplatedNode) == 0x48, "C_TemplatedNode size mismatch");
} // namespace SDK::wh::conceptmodule
