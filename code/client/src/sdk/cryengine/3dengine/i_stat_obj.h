#pragma once

#include "../../types.h"
#include "../physics/i_physical_entity.h"
#include "../physics/pe_geomparams.h"
#include "i_material.h"

#include <cstddef>

namespace SDK {
    /**
     * A loaded `.cgf`, as `I3DEngine::LoadStatObj` hands it out and
     * `IRenderNode::SetEntityStatObj` takes it.
     *
     * Mostly opaque. `_smart_ptr<IStatObj>::operator=` (0x180611A20) -- the
     * assignment `CProceduralClipAttachProp::OnEnter` fills a
     * `CCGFAttachment`'s mesh with -- calls slot 1 on the incoming mesh and
     * slot 2 on the outgoing one. A node's world bounds come off
     * `IShadowCaster::FillBBox` rather than off the mesh.
     *
     * The mesh's own bounds are `CStatObj::m_AABB`, min at +0x100 and max at
     * +0x10C: the table at 0x183A2F888 holds stock `SetBBoxMin` and
     * `SetBBoxMax` side by side at slots 33 and 34 (0x181AAE390, 0x181AAE370),
     * each writing one of those two, and slot 4 copies the pair out whole.
     *
     * Borrowed, not owned: `LoadStatObj` hands out a mesh without a share.
     * `CBrush::SetEntityStatObj` (0x180A75FA0) takes its own through the
     * smart pointer at `CBrush+0x98`, and a `CCGFAttachment` through its own.
     *
     * The `CStatObj` table at 0x183A2F888 has 90 entries. Slots 41 to 44 are
     * stock CryEngine's path and geometry-name pairs, each getter one
     * `mov rax, [rcx+X]` and each setter a `CryStringT::operator=` on the
     * same field: the file path at `CStatObj+0xA0`, the geometry name at
     * `+0xA8`. `CVegetation::GetName` (0x18354036C) answers with that same
     * `+0xA0`.
     */
    class IStatObj {
      public:
        static constexpr int kCompound = 0x20;

        virtual ~IStatObj() = default;
        virtual void AddRef()  = 0; // +0x08
        virtual void Release() = 0; // +0x10
        virtual void unk_fn0003() = 0;
        /** The mesh's bounds in its own space, unscaled; the stock `GetAABB`, returned through the hidden pointer. */
        virtual AABB GetAABB() const = 0; // +0x20, 0x1803FCC18
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        /** The mesh's own material, the one a node without an override draws with. `CVegetation` and `CBrush` fall back to it the same way. */
        virtual IMaterial *GetMaterial() const = 0; // +0x48
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual int GetFlags() const = 0; // +0x80, 0x181A741F0
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        /** The `.cgf` it was loaded from, as the level spells it: `objects/natural/vegetation/trees/...`. */
        virtual const char *GetFilePath() const = 0; // +0x148
        virtual void unk_fn0042()               = 0;
        virtual void unk_fn0043()               = 0;
        virtual void unk_fn0044()               = 0;
        virtual void unk_fn0045()               = 0;
        virtual void unk_fn0046()               = 0;
        /**
         * Model-space helper transform, or identity when absent. Slot 47,
         * CStatObj::GetHelperTM (0x180B31090). Native hand attachment reads
         * slt_0 through 0x180B309DC and inverts it in 0x181E7FB20 before
         * SetAttRelativeDefault. This aligns the grip rather than the mesh
         * origin with the animated hand.
         */
        virtual const Matrix34 &GetHelperTM(const char *name) const = 0; // +0x178
        virtual void unk_fn0048()               = 0;
        virtual void unk_fn0049()               = 0;
        virtual void unk_fn0050()               = 0;
        virtual void unk_fn0051()               = 0;
        virtual void unk_fn0052()               = 0;
        /** Checks this mesh only; compound children can have geometry when this is false. */
        virtual bool IsPhysicsExist() = 0; // +0x1A8, 0x180530DA0
        virtual void unk_fn0054()     = 0;
        virtual void unk_fn0055()     = 0;
        virtual void unk_fn0056()     = 0;
        virtual void unk_fn0057()     = 0;
        virtual void unk_fn0058()     = 0;
        virtual void unk_fn0059()     = 0;
        virtual void unk_fn0060()     = 0;
        virtual void unk_fn0061()     = 0;
        virtual void unk_fn0062()     = 0;
        virtual void unk_fn0063()     = 0;
        virtual void unk_fn0064()     = 0;
        virtual void unk_fn0065()     = 0;
        virtual void unk_fn0066()     = 0;
        virtual void unk_fn0067()     = 0;
        virtual void unk_fn0068()     = 0;
        /** Includes compound children, unlike IsPhysicsExist. 0x1804AA744. */
        virtual int Physicalize(IPhysicalEntity *entity, pe_geomparams *params, int id = -1, const char *propertiesOverride = nullptr) = 0; // +0x228
    };

    static_assert(sizeof(IStatObj) == 0x8, "IStatObj size mismatch");
} // namespace SDK
