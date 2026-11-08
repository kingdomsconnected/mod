#pragma once

namespace SDK::wh::guimodule {
    /**
     * Inventory UI context interface, identified by RTTI. The secondary
     * table at 0x183EDE980 has five slots and no destructor slot. Their
     * complete signatures are not established; reserve them without calling
     * them until each signature is separately proven.
     */
    class I_UIItemContext {
      protected:
        virtual void unk_context_fn0000() = 0;
        virtual void unk_context_fn0001() = 0;
        virtual void unk_context_fn0002() = 0;
        virtual void unk_context_fn0003() = 0;
        virtual void unk_context_fn0004() = 0;
    };

    static_assert(sizeof(I_UIItemContext) == 0x8, "I_UIItemContext size mismatch");
} // namespace SDK::wh::guimodule
