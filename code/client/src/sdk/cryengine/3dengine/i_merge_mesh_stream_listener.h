#pragma once

namespace SDK {
    /**
     * Listener used by streamed merged-mesh render nodes.
     *
     * RTTI proves this as C_RespawnManager's secondary base at +0x08. The
     * concrete secondary table at 0x183AB9650 contains exactly two callbacks;
     * neither stripped source signature is named in the binary.
     */
    class IMergeMeshStreamListener {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
    };

    static_assert(sizeof(IMergeMeshStreamListener) == 0x8,
                  "IMergeMeshStreamListener size mismatch");
} // namespace SDK
