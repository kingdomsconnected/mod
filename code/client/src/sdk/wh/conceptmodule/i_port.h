#pragma once

#include "c_shared_resource.h"

#include "../../types.h"

#include <cstddef>

namespace SDK::wh::conceptmodule {
    /**
     * A value or trigger slot on a node. Being a `C_SharedResource` it has a
     * registry id of its own, which is what lets one node's port reference
     * another node's.
     *
     * `m_pDefinition` is the authored description the deserializer attaches,
     * and what `C_PortRef` consults for the name to look up -- which is why a
     * reference with neither a definition nor a resolved link faults rather
     * than answering empty.
     *
     * The two proven virtuals (9 `GetName`, 16 `GetValue`) are called only
     * from inside the game, so they are not spelled here.
     */
    class I_Port: public C_SharedResource {
      public:
        /** The node this port hangs off, unset until `C_Node::AttachPort` binds it. */
        const S_WeakResourceId &GetOwner() const {
            return m_owner;
        }

        const SDK::CryStringT<char> &GetName() const {
            return m_name;
        }

        /** Whether the deserializer left an authored description on this port. */
        bool HasDefinition() const {
            return m_pDefinition != nullptr;
        }

      private:
        S_WeakResourceId m_owner;     // 0x10, id -1 and generation 0 until AddPort
        void *m_pDefinition;          // 0x18, shared_ptr<definition::I_PortDefinition>
        void *m_pDefinitionState;     // 0x20, its control block
        SDK::CryStringT<char> m_name; // 0x28

        static void AssertLayout() {
            static_assert(offsetof(I_Port, m_owner) == 0x10, "I_Port::m_owner offset mismatch");
            static_assert(offsetof(I_Port, m_pDefinition) == 0x18, "I_Port::m_pDefinition offset mismatch");
            static_assert(offsetof(I_Port, m_name) == 0x28, "I_Port::m_name offset mismatch");
        }
    };

    static_assert(sizeof(I_Port) == 0x30, "I_Port size mismatch");
} // namespace SDK::wh::conceptmodule
