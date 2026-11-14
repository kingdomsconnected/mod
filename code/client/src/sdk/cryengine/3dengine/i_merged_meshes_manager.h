#pragma once

#include "i_merge_mesh_stream_listener.h"

namespace SDK {
    /**
     * The 3D engine's owner of every merged-mesh sector: the terrain detail
     * the level bakes out of its `AutoMerged` vegetation groups -- tall
     * grass, flowers, ferns, herbs.
     *
     * The abstract table at 0x18402A3B8 holds the destructor and 22 pure
     * operations; `CMergedMeshesManager`'s is 0x183C24CB8. Only the listener
     * pair is named, which KCD2 added for its respawn manager and stock
     * CryEngine does not have:
     *
     * - 0x181A6E820 appends the pointer to the vector at manager +0xC1F0.
     * - 0x1834ED1C4 erases every copy of it from that vector.
     *
     * `C_RespawnManager`'s destructor (0x182ADCCDC) removes itself through
     * +0x88, on the manager `I3DEngine::GetIMergedMeshesManager` returns.
     * Neither takes a lock: main thread only, and never from inside a
     * listener callback, which is called while the list is being walked.
     */
    class IMergedMeshesManager {
      public:
        virtual ~IMergedMeshesManager() = default;
        virtual void unk_fn0001()       = 0;
        virtual void unk_fn0002()       = 0;
        virtual void unk_fn0003()       = 0;
        virtual void unk_fn0004()       = 0;
        virtual void unk_fn0005()       = 0;
        virtual void unk_fn0006()       = 0;
        virtual void unk_fn0007()       = 0;
        virtual void unk_fn0008()       = 0;
        virtual void unk_fn0009()       = 0;
        virtual void unk_fn0010()       = 0;
        virtual void unk_fn0011()       = 0;
        virtual void unk_fn0012()       = 0;
        virtual void unk_fn0013()       = 0;
        virtual void unk_fn0014()       = 0;
        virtual void unk_fn0015()       = 0;
        virtual void AddStreamListener(IMergeMeshStreamListener *listener)    = 0; // +0x80
        virtual void RemoveStreamListener(IMergeMeshStreamListener *listener) = 0; // +0x88
        virtual void unk_fn0018()       = 0;
        virtual void unk_fn0019()       = 0;
        virtual void unk_fn0020()       = 0;
        virtual void unk_fn0021()       = 0;
        virtual void unk_fn0022()       = 0;
    };

    static_assert(sizeof(IMergedMeshesManager) == 0x8,
                  "IMergedMeshesManager size mismatch");
} // namespace SDK
