#pragma once

#include "c_shared_resource.h"
#include "i_port.h"

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::conceptmodule {
    /**
     * A vertex of the concept graph: a named resource that owns a list of
     * ports. Quests, objectives, activities and every piece of authored
     * gameplay logic are one of these.
     *
     * The name at 0x10 is the identity the rest of the game uses --
     * `C_QuestManager::FindQuestByName` does a plain strcmp against it -- and
     * the port vector at 0x28 is what `C_PortRef` resolution searches when a
     * reference has no resolved target cached.
     */
    class C_Node: public C_SharedResource {
      public:
        const SDK::CryStringT<char> &GetName() const {
            return m_name;
        }

        /** Assigns the node name through the engine's own string operator, so the buffer stays the game's. */
        void SetName(const char *name) {
            m_name.Assign(name);
        }

        const SDK::StdVector<I_Port *> &GetPorts() const {
            return m_ports;
        }

        /**
         * Hands the node a port, refusing a name it already carries. The native
         * consumes one reference either way, so pass one you are giving up.
         *
         * Named `AttachPort` because `AddPort` is a winspool.h macro that
         * rewrites itself to `AddPortA` wherever windows.h came first.
         */
        bool AttachPort(I_Port *port);

        /** The port of that name, with a reference the caller owns, or null. */
        I_Port *FindPortByName(const SDK::CryStringT<char> &name) const;

      private:
        SDK::CryStringT<char> m_name; // 0x10
        std::int32_t m_unknown0018;   // 0x18, constructed as 1
        std::int32_t m_unknown001C;   // 0x1C, constructed as -1
        bool m_unknown0020;           // 0x20
        std::byte m_padding0021[0x7];
        SDK::StdVector<I_Port *> m_ports; // 0x28

        static void AssertLayout() {
            static_assert(offsetof(C_Node, m_name) == 0x10, "C_Node::m_name offset mismatch");
            static_assert(offsetof(C_Node, m_unknown0018) == 0x18, "C_Node::m_unknown0018 offset mismatch");
            static_assert(offsetof(C_Node, m_ports) == 0x28, "C_Node::m_ports offset mismatch");
        }
    };

    static_assert(sizeof(C_Node) == 0x40, "C_Node size mismatch");
} // namespace SDK::wh::conceptmodule
