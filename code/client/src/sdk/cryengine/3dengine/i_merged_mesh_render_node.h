#pragma once

namespace SDK {
    /**
     * KCD2's game-facing side of a merged-mesh sector, which stock CryEngine
     * does not have. RTTI places it in `CMergedMeshRenderNode` at +0x50, and
     * `IRenderNode` +0x208 answers with it for a merged-mesh node and with
     * null for every other (0x1808303D8, base 0x18066CD10).
     *
     * The concrete table at 0x18402A780 holds three entries and no
     * destructor. The first two walk the sector's instances for the respawn
     * manager and take a `std::function`, so they stay opaque here:
     *
     * - 0x18043C258 hides (scale byte to zero) or, with its last argument
     *   set, restores every instance its predicate accepts.
     * - 0x18043BFEC only visits.
     *
     * Both skip any group whose `IStatInstGroup::pickable` is clear, so
     * neither reaches the grass: only what the player can gather.
     */
    class IMergedMeshRenderNode {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;

        /** Whether the instances are in memory: the node's state is 5 (0x18043B52C). */
        virtual bool IsStreamedIn() const = 0; // +0x10
    };

    static_assert(sizeof(IMergedMeshRenderNode) == 0x8,
                  "IMergedMeshRenderNode size mismatch");
} // namespace SDK
