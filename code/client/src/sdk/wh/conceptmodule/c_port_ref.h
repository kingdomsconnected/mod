#pragma once

#include "i_port.h"

#include <cstddef>

namespace SDK::wh::conceptmodule {
    /**
     * A node member that reads another port's value: a quest's progress, an
     * objective's tracker. It is itself a port, and it holds nothing but the
     * cache of what it last resolved to.
     *
     * `GetValue` (slot 16) resolves the link and delegates to the target's
     * own. Resolution reads the owner node id and the port name out of the
     * definition, so a reference whose definition is null dereferences null on
     * the very first read.
     *
     * `BindResolvedTarget` writes the answer straight into the cache, which is
     * the state the game's own resolver leaves behind -- and the only way into
     * a port reference that has no authored definition.
     */
    class C_PortRef: public I_Port {
      public:
        bool IsLinkResolved() const {
            return m_linkResolved;
        }

        I_Port *GetLinkTarget() const {
            return m_pLinkTarget;
        }

        /** Pins this reference to `target`, taking the reference the cache holds. With the cache filled, resolution never consults the definition. */
        void BindResolvedTarget(I_Port *target) {
            if (m_pLinkTarget != nullptr) {
                m_pLinkTarget->Release();
            }

            m_pLinkTarget  = target;
            m_linkResolved = target != nullptr;

            if (target != nullptr) {
                target->AddRef();
            }
        }

      private:
        bool m_linkResolved; // 0x30
        std::byte m_padding0031[0x7];
        I_Port *m_pLinkTarget; // 0x38

        static void AssertLayout() {
            static_assert(offsetof(C_PortRef, m_linkResolved) == 0x30, "C_PortRef::m_linkResolved offset mismatch");
            static_assert(offsetof(C_PortRef, m_pLinkTarget) == 0x38, "C_PortRef::m_pLinkTarget offset mismatch");
        }
    };

    static_assert(sizeof(C_PortRef) == 0x40, "C_PortRef size mismatch");

    /** `C_TypedPortRef<T>` adds no storage; it swaps in a vtable whose `GetValue` converts to `T`. */
    template <typename Value>
    class C_TypedPortRef: public C_PortRef {};

    static_assert(sizeof(C_TypedPortRef<int>) == 0x40, "C_TypedPortRef size mismatch");
} // namespace SDK::wh::conceptmodule
