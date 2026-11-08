#pragma once

namespace SDK::wh::guimodule {
    /** Root native game-UI object. */
    class C_UIBase {
      public:
        virtual ~C_UIBase() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;

      private:
        void *m_pRuntimeState; // 0x08
    };

    static_assert(sizeof(C_UIBase) == 0x10, "C_UIBase size mismatch");
} // namespace SDK::wh::guimodule
