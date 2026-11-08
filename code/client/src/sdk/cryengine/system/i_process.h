#pragma once

namespace SDK {
    /**
     * Base process interface used by I3DEngine.
     *
     * The abstract IProcess table at 0x184020FA0 contains exactly seven
     * entries. The binary does not retain trustworthy source-level names for
     * its six operations, so they remain opaque.
     */
    class IProcess {
      public:
        virtual ~IProcess() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0; // +0x030
    };

    static_assert(sizeof(IProcess) == 0x8, "IProcess size mismatch");
} // namespace SDK
