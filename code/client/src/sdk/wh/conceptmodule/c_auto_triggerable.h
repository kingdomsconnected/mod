#pragma once

#include "c_node.h"

namespace SDK::wh::conceptmodule {
    /**
     * Marks a node the graph may fire without an incoming trigger. It adds no
     * storage: the constructor only sets a bit in the flags word that
     * `C_SharedResource` already carries at 0x0E.
     */
    template <typename BaseNode>
    class C_AutoTriggerable: public BaseNode {};
} // namespace SDK::wh::conceptmodule
