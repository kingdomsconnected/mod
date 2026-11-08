#pragma once

#include "../../types.h"
#include "i_shadow_caster.h"
#include "i_stat_obj.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryEngine's ERF_*, a 64-bit field on every node. Only the ones this mod
     * acts on are named; KCD2 adds its own above bit 44, two of which join
     * ERF_HIDDEN in the engine's "do not draw" mask.
     */
    enum ERenderNodeFlags : std::uint64_t {
        /**
         * Draws into the shadow maps. Always set with ERF_HAS_CASTSHADOWMAPS,
         * as `CScriptBind_Entity::RenderShadow` (0x1837DBDBD) does: it ORs
         * 0x40000008 onto the node.
         */
        ERF_CASTSHADOWMAPS     = 1ULL << 3,
        ERF_HAS_CASTSHADOWMAPS = 1ULL << 30,

        ERF_HIDDEN     = 1ULL << 8,
        ERF_NO_PHYSICS = 1ULL << 17,

        /**
         * Set and cleared by `COctreeNode::ActivateObjectsLayer` (0x1805DF388)
         * on every brush of a layer it switches, together with ERF_HIDDEN and
         * the collider. Never touched for a node on no switchable layer.
         */
        ERF_ACTIVE_LAYER = 1ULL << 31,

        /**
         * KCD2's own: the brush's collider is built by
         * `C_PhysicalizationManager` from the layer's physics file, not by the
         * brush. `CBrush::PhysicalizeOnHeap` (0x180530A3C) returns at once for
         * a brush carrying it while `wh_e_PernamentPhysicalization` is on, so
         * its own physics stays null; cleared, the brush physicalizes itself
         * the stock way, and `SetMatrix` and `Dephysicalize` then move and
         * drop that collider as they do everywhere else.
         */
        ERF_LAYER_PHYSICS = 1ULL << 62,
    };

    class IEntity;
    struct IMaterial;
    struct IPhysicalEntity;

    /**
     * Base interface and shared state for 3D-engine render nodes.
     *
     * The abstract table at 0x183A33AB8 contains 73 entries: all eight
     * IShadowCaster entries followed by 65 IRenderNode operations. Its scalar
     * deleting destructor releases exactly 0x50 bytes, proving the shared
     * render-node state following the 0x10-byte IShadowCaster base.
     */
    class IRenderNode : public IShadowCaster {
      public:
        ~IRenderNode() override = default;

        virtual void unk_fn0008() = 0;
        /**
         * For a brush this is its `.cgf` path, the only name static geometry
         * has; a node with no mesh answers with a placeholder rather than
         * null. A plain const char *, safe to read from here.
         */
        virtual const char *GetName() const = 0; // +0x48
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual const Matrix34 &GetMatrix() const = 0; // +0x78
        virtual void SetMatrix(const Matrix34 &matrix) = 0; // +0x80
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void SetEntityStatObj(IStatObj *pStatObj,
                                      const Matrix34 *pMatrix = nullptr) = 0; // +0xC0
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        /** Stores the LOD ratio, through the engine-lock buffer for an entity's render proxy (0x1808F092C). */
        virtual void SetLodRatio(int ratio) = 0; // +0xE0
        virtual void unk_fn0029() = 0;
        virtual IPhysicalEntity *GetPhysics() const = 0; // +0xF0
        virtual void SetPhysics(IPhysicalEntity *pPhysicalEntity) = 0; // +0xF8
        /**
         * Asks for physics without insisting. `CBrush`'s (0x18348F758) drops
         * the argument and passes `instant = false` to `PhysicalizeOnHeap`,
         * so a brush under `e_OnDemandMaxSize` is only registered in the
         * physics on-demand grid -- which builds nothing for a brush added to
         * a cell the player already stands in. Use `PhysicalizeNow`.
         */
        virtual void Physicalize(bool instant = false) = 0; // +0x100
        /**
         * Builds the node's physics, `instant` honoured. `CBrush`'s
         * (0x180530A28) clears the physicalization-disabled bit and calls
         * `PhysicalizeOnHeap(this, nullptr, instant)`; it is also what
         * `COctreeNode::CheckPhysicalized` (0x180530704) and
         * `CPhysStreamer::CreatePhysicalEntity` call, with `true`, to
         * service the on-demand grid. Never call it on a node whose
         * physicalization was disabled on purpose: it re-enables it.
         */
        virtual void PhysicalizeNow(bool instant) = 0; // +0x108
        virtual bool PhysicalizeFoliage(bool physicalize = true,
                                        int source = 0,
                                        int slot = 0) = 0; // +0x110
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void SetMaterial(IMaterial *pMaterial) = 0; // +0x128
        virtual IMaterial *GetMaterial(Vec3 *pHitPosition = nullptr) = 0; // +0x130
        virtual IMaterial *GetMaterialOverride() = 0; // +0x138
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        /** The object layer the node belongs to; `CBrush` keeps it at +0x92. */
        virtual void SetLayerId(std::uint16_t layerId) = 0; // +0x158
        /** Compared against the switched layer by `COctreeNode::ActivateObjectsLayer`. */
        virtual std::uint16_t GetLayerId() = 0; // +0x160
        /**
         * How far off the node is drawn: max(`e_ViewDistMin`, radius x
         * `e_ViewDistRatio` x view-distance ratio / 100) -- `CBrush`
         * 0x1805E0EF8, `CRenderProxy` 0x1805E11E4.
         */
        virtual float GetMaxViewDist() = 0; // +0x168
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        /**
         * Drops the node's own physics. `CBrush`'s (0x180531038) removes
         * +0x80 through the physical world; `SetMatrix`,
         * `PhysicalizeOnHeap` and `COctreeNode::ActivateObjectsLayer` call
         * it directly.
         */
        virtual void Dephysicalize(bool keepIfReferenced = false) = 0; // +0x180
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0;
        virtual void unk_fn0052() = 0;
        virtual void unk_fn0053() = 0;
        virtual void unk_fn0054() = 0;
        virtual void unk_fn0055() = 0;
        virtual void unk_fn0056() = 0;
        virtual void unk_fn0057() = 0;
        virtual void unk_fn0058() = 0;
        /**
         * The view-distance ratio, clamped to 0-255 and kept at +0x4D. The
         * base (0x1805DECB0) re-sorts the node in its octree; an entity's
         * render proxy (0x1812B0FEC) applies it to every slot's node under
         * the engine lock, or queues it to the lock buffer. 255 draws the
         * node a hundred times as far as the default ratio, not without
         * limit. Never write +0x4D directly: that skips both.
         */
        virtual void SetViewDistRatio(int ratio) = 0; // +0x1D8
        virtual void unk_fn0060() = 0;
        virtual void unk_fn0061() = 0;
        virtual void unk_fn0062() = 0;
        virtual void unk_fn0063() = 0;
        virtual void unk_fn0064() = 0;
        virtual void unk_fn0065() = 0;
        virtual void SetOwnerEntity(IEntity *ownerEntity) = 0; // +0x210
        virtual IEntity *GetOwnerEntity() const = 0; // +0x218
        virtual void unk_fn0068() = 0;

        /**
         * Re-sorts this node inside its octree node to match its flags. The
         * reorder half of CryEngine's Hide(bool), which KCD2 split from the
         * flag write, so hiding is two steps.
         *
         * Both are needed: the octree's render walk is
         * `for (node = first; node && node->IsRenderable(); node = next)`, so
         * it stops at the first node that will not draw rather than skipping
         * it. Setting the flag alone would blank everything behind it.
         */
        virtual void ReorderForRenderVisibility() = 0; // +0x228
        virtual void unk_fn0070() = 0;
        virtual void unk_fn0071() = 0;
        virtual void unk_fn0072() = 0;

        std::uint64_t GetRenderFlags() const {
            return m_renderFlags;
        }

        bool IsHidden() const {
            return (m_renderFlags & ERF_HIDDEN) != 0;
        }

        /** Sets or clears ERF_HIDDEN and re-sorts the node to match. */
        void SetHidden(bool hidden) {
            if (hidden) {
                m_renderFlags |= ERF_HIDDEN;
            }
            else {
                m_renderFlags &= ~static_cast<std::uint64_t>(ERF_HIDDEN);
            }
            ReorderForRenderVisibility();
        }

        /**
         * Adds flags to a node the 3D engine has not registered yet. A
         * registered node is sorted by its flags, so it would need
         * re-registering; one being built is registered after this anyway.
         */
        void AddRenderFlagsBeforeRegistration(std::uint64_t flags) {
            m_renderFlags |= flags;
        }

        /**
         * Hands a layer brush's collision to the brush itself, or back to the
         * layer physics (see ERF_LAYER_PHYSICS). Only the flag: the caller
         * physicalizes or dephysicalizes to match.
         */
        void SetLayerPhysics(bool layerPhysics) {
            if (layerPhysics) {
                m_renderFlags |= ERF_LAYER_PHYSICS;
            }
            else {
                m_renderFlags &= ~static_cast<std::uint64_t>(ERF_LAYER_PHYSICS);
            }
        }

      private:
        // Members follow stock IRenderNode; the constructor 0x180422D34 sets
        // 0x38 to -1 and writes "dd" (100, 100, 0) at 0x4D, which pins the
        // view-distance and LOD ratios. KCD2 has no editor selection id.
        IRenderNode *m_pNext;                     // 0x10, the octree sector's node list
        IRenderNode *m_pPrev;                     // 0x18
        void *m_pOctreeNode;                      // 0x20, IOctreeNode *
        std::uint64_t m_renderFlags;              // 0x28
        void *m_pTempData;                        // 0x30, SRenderNodeTempData *
        std::int32_t m_manipulationFrame;         // 0x38, -1
        std::uint32_t m_nHUDSilhouettesParam;     // 0x3C
        std::uint64_t m_onePassData;              // 0x40, the one-pass traversal frame id and shadow cascades
        float m_fWSMaxViewDist;                   // 0x48
        std::uint8_t m_nInternalFlags;            // 0x4C
        std::uint8_t m_ucViewDistRatio;           // 0x4D, 100
        std::uint8_t m_ucLodRatio;                // 0x4E, 100
        std::uint8_t m_nMaterialLayers;           // 0x4F

        static void AssertLayout() {
            static_assert(offsetof(IRenderNode, m_pOctreeNode) == 0x20, "IRenderNode octree-node offset mismatch");
            static_assert(offsetof(IRenderNode, m_renderFlags) == 0x28, "IRenderNode render-flags offset mismatch");
            static_assert(offsetof(IRenderNode, m_manipulationFrame) == 0x38, "IRenderNode::m_manipulationFrame offset mismatch");
            static_assert(offsetof(IRenderNode, m_onePassData) == 0x40, "IRenderNode::m_onePassData offset mismatch");
            static_assert(offsetof(IRenderNode, m_ucViewDistRatio) == 0x4D, "IRenderNode::m_ucViewDistRatio offset mismatch");
        }
    };

    static_assert(sizeof(IRenderNode) == 0x50,
                  "IRenderNode size mismatch");
} // namespace SDK
