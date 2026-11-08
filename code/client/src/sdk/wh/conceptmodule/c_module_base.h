#pragma once

#include "c_auto_triggerable.h"
#include "c_node.h"

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::conceptmodule {
    /**
     * A node that is itself a sub-graph: it owns child nodes and the bookkeeping
     * the graph runtime keeps for a scope. Quests, activity collections and
     * activity types are all one of these, and their own members start at 0xA0.
     *
     * None of the three vectors between 0x40 and 0x88 has an identified
     * element type; the ctor (0x1806B193C) zeroes all nine qwords.
     */
    class C_ModuleBase: public C_AutoTriggerable<C_Node> {
      private:
        SDK::StdVector<void *> m_vector0040; // 0x40
        SDK::StdVector<void *> m_vector0058; // 0x58
        SDK::StdVector<void *> m_vector0070; // 0x70
        SDK::CryStringT<char> m_unknown0088; // 0x88
        SDK::CryStringT<char> m_unknown0090; // 0x90
        std::uint16_t m_unknown0098;         // 0x98
        std::byte m_padding009A[0x1];
        bool m_unknown009B;         // 0x9B
        std::int32_t m_unknown009C; // 0x9C, constructed as INT32_MAX

        static void AssertLayout() {
            static_assert(offsetof(C_ModuleBase, m_vector0040) == 0x40, "C_ModuleBase::m_vector0040 offset mismatch");
            static_assert(offsetof(C_ModuleBase, m_vector0070) == 0x70, "C_ModuleBase::m_vector0070 offset mismatch");
            static_assert(offsetof(C_ModuleBase, m_unknown0088) == 0x88, "C_ModuleBase::m_unknown0088 offset mismatch");
            static_assert(offsetof(C_ModuleBase, m_unknown009C) == 0x9C, "C_ModuleBase::m_unknown009C offset mismatch");
        }
    };

    static_assert(sizeof(C_ModuleBase) == 0xA0, "C_ModuleBase size mismatch");
} // namespace SDK::wh::conceptmodule
