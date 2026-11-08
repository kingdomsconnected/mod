#pragma once

#include <cstddef>

namespace SDK::wh::xgenaimodule {
    /**
     * Small polymorphic holder embedded by linkable game-object extensions.
     *
     * Its table at 0x183A651A8 contains exactly seven entries. Construction,
     * scalar deletion, and use at C_LinkableObjectExtension +0x40 prove the
     * complete 0x18-byte extent. The stripped holder operations remain opaque.
     */
    class C_LinkableObjectHolder {
      public:
        virtual ~C_LinkableObjectHolder() = default; // +0x00
        virtual void unk_fn0001();
        virtual void unk_fn0002();
        virtual void unk_fn0003();
        virtual void unk_fn0004();
        virtual void unk_fn0005();
        virtual void unk_fn0006(); // +0x30

        void *GetLinkedObjectRaw() const { return m_pLinkedObject; }

      private:
        void *m_pLinkedObject; // 0x08
        void *m_pHolderState;  // 0x10
    };

    static_assert(sizeof(C_LinkableObjectHolder) == 0x18,
                  "C_LinkableObjectHolder size mismatch");
} // namespace SDK::wh::xgenaimodule
