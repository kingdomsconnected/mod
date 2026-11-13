#pragma once

#include "i_render_node.h"

namespace SDK {
    /**
     * Told when a merged-mesh sector's instances come and go.
     *
     * RTTI proves this as C_RespawnManager's secondary base at +0x08. The
     * concrete secondary table at 0x183AB9650 contains exactly two callbacks;
     * the binary names neither, so the names say when each is called.
     *
     * `IMergedMeshesManager` keeps the listeners and both callers walk its
     * list in order, on the thread that completes the stream request:
     *
     * - 0x18043AA54, from the node's `IStreamCallback` completion
     *   (0x18043A9B8) straight after it sets the node's state to streamed
     *   in, so the instances are readable and nothing has drawn them yet.
     * - 0x1834EBD00, from the node's stream-out (0x1834EF368) before it
     *   frees the instance arrays and nulls them.
     *
     * The node is the `CMergedMeshRenderNode` itself. C_RespawnManager's own
     * callbacks (0x18043AAA4, 0x182ADEA68) read and write its hash maps with
     * no lock, which is what says the calls come on the main thread.
     */
    class IMergeMeshStreamListener {
      public:
        virtual void OnMergedMeshStreamedIn(IRenderNode *node)  = 0; // +0x00
        virtual void OnMergedMeshStreamedOut(IRenderNode *node) = 0; // +0x08
    };

    static_assert(sizeof(IMergeMeshStreamListener) == 0x8,
                  "IMergeMeshStreamListener size mismatch");
} // namespace SDK
