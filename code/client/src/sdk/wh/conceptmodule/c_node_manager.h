#pragma once

#include "c_shared_resource.h"

#include "../../types.h"
#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::conceptmodule {
    /**
     * Owns a set of nodes as weak handles rather than as pointers, so a node
     * the registry has dropped simply stops resolving.
     *
     * The quest manager's construction in C_QuestModule::Init (0x18182B864)
     * memsets the 0x78 bytes, then writes four C_Signals (dispatch
     * unk_1856691D8, both handles -1) at 0x08..0x48 and zeroes the vector.
     */
    class C_NodeManager {
      public:
        virtual ~C_NodeManager() = default;

        /** The nodes this manager holds, as weak handles into the concept resource registry. */
        const SDK::StdVector<S_WeakResourceId> &GetNodes() const {
            return m_nodes;
        }

      private:
        SDK::wh::shared::C_Signal m_signals0008[4]; // 0x08
        SDK::StdVector<S_WeakResourceId> m_nodes; // 0x48
        std::uint64_t m_state0060[3];               // 0x60, only the allocation memset writes it

        static void AssertLayout() {
            static_assert(offsetof(C_NodeManager, m_signals0008) == 0x08, "C_NodeManager::m_signals0008 offset mismatch");
            static_assert(offsetof(C_NodeManager, m_nodes) == 0x48, "C_NodeManager::m_nodes offset mismatch");
            static_assert(offsetof(C_NodeManager, m_state0060) == 0x60, "C_NodeManager::m_state0060 offset mismatch");
        }
    };

    static_assert(sizeof(C_NodeManager) == 0x78, "C_NodeManager size mismatch");
} // namespace SDK::wh::conceptmodule
