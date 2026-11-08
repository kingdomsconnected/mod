#pragma once

#include "../../types.h"
#include "cry_3d_engine_base.h"
#include "i_brush.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct IDeformableNode;
    struct IFoliage;
    struct IMaterial;
    struct IPhysicalEntity;

    /** Native copy-on-write compound sub-object visibility mask. */
    struct BrushSubObjectHideMaskLayout {
        std::uint32_t *m_pValues; // 0x00
        std::uint32_t m_countAndPolicy; // 0x08, high bit selects shared storage

        std::uint32_t GetCount() const {
            return m_countAndPolicy & 0x7FFFFFFFU;
        }
        bool UsesSharedStorage() const {
            return (m_countAndPolicy & 0x80000000U) != 0;
        }
    };

    static_assert(sizeof(BrushSubObjectHideMaskLayout) == 0x10,
                  "BrushSubObjectHideMaskLayout size mismatch");

    /** Opaque native optional<SCameraSpaceParams> payload. */
    struct BrushCameraSpaceParamsLayout {
        std::byte m_storage[0x18];
        bool m_hasValue; // 0x18
        std::byte m_padding0019[0x7];
    };

    static_assert(sizeof(BrushCameraSpaceParamsLayout) == 0x20,
                  "BrushCameraSpaceParamsLayout size mismatch");

    /** Exact constructor/destructor-proven CBrush state following IBrush. */
    struct BrushRuntimeStateLayout {
        Matrix34 m_worldTransform; // 0x00
        IPhysicalEntity *m_pPhysicalEntity; // 0x30
        IMaterial *m_pMaterial; // 0x38, retained reference
        std::uint16_t m_collisionClassIndex; // 0x40
        std::uint16_t m_layerId; // 0x42
        std::byte m_padding0044[0x4];
        IStatObj *m_pStatObj; // 0x48, retained reference
        IDeformableNode *m_pDeformableNode; // 0x50
        IFoliage *m_pFoliage; // 0x58
        std::uint32_t m_renderFlags; // 0x60, bit 0 draw-last, bit 1 no-physicalize
        AABB m_worldBounds; // 0x64
        std::uint32_t m_lastMoveFrameId; // 0x7C
        BrushSubObjectHideMaskLayout m_subObjectHideMask; // 0x80
        BrushCameraSpaceParamsLayout m_cameraSpaceParams; // 0x90

        Vec3 GetPosition() const {
            return {m_worldTransform.m[0][3],
                    m_worldTransform.m[1][3],
                    m_worldTransform.m[2][3]};
        }
    };

    static_assert(offsetof(BrushRuntimeStateLayout, m_pPhysicalEntity) == 0x30,
                  "CBrush physical-entity offset mismatch");
    static_assert(offsetof(BrushRuntimeStateLayout, m_pMaterial) == 0x38,
                  "CBrush material offset mismatch");
    static_assert(offsetof(BrushRuntimeStateLayout, m_layerId) == 0x42,
                  "CBrush layer-id offset mismatch");
    static_assert(offsetof(BrushRuntimeStateLayout, m_pStatObj) == 0x48,
                  "CBrush stat-object offset mismatch");
    static_assert(offsetof(BrushRuntimeStateLayout, m_worldBounds) == 0x64,
                  "CBrush bounds offset mismatch");
    static_assert(offsetof(BrushRuntimeStateLayout, m_subObjectHideMask) == 0x80,
                  "CBrush sub-object hide-mask offset mismatch");
    static_assert(sizeof(BrushRuntimeStateLayout) == 0xB0,
                  "BrushRuntimeStateLayout size mismatch");

    /**
     * Concrete static-geometry render node used for spawned map props.
     *
     * RTTI proves IBrush at +0x00 and the empty Cry3DEngineBase tag at +0x50.
     * The scalar deleting destructor returns one 0x100-byte object to the
     * fixed render-node allocator. Construction initializes an identity
     * Matrix34 at +0x50, retained render resources, flags, bounds, and the
     * auxiliary-value buffer through the terminal state byte at +0xF8.
     */
    class CBrush : public IBrush, public Cry3DEngineBase {
      public:
        ~CBrush() override = default;

        const Matrix34 &GetWorldTransform() const {
            return m_runtimeState.m_worldTransform;
        }
        Vec3 GetPosition() const { return m_runtimeState.GetPosition(); }
        const AABB &GetWorldBounds() const {
            return m_runtimeState.m_worldBounds;
        }
        IPhysicalEntity *GetPhysicalEntity() const {
            return m_runtimeState.m_pPhysicalEntity;
        }
        IMaterial *GetMaterialRaw() const { return m_runtimeState.m_pMaterial; }
        IStatObj *GetStatObj() const { return m_runtimeState.m_pStatObj; }
        std::uint32_t GetRenderFlags() const {
            return m_runtimeState.m_renderFlags;
        }
        bool IsDrawLast() const {
            return (m_runtimeState.m_renderFlags & 0x1U) != 0;
        }
        bool IsPhysicalizationDisabled() const {
            return (m_runtimeState.m_renderFlags & 0x2U) != 0;
        }
        std::uint16_t GetCollisionClassIndexRaw() const {
            return m_runtimeState.m_collisionClassIndex;
        }
        std::uint16_t GetLayerIdRaw() const {
            return m_runtimeState.m_layerId;
        }
        IFoliage *GetFoliage() const { return m_runtimeState.m_pFoliage; }
        const BrushSubObjectHideMaskLayout &GetSubObjectHideMask() const {
            return m_runtimeState.m_subObjectHideMask;
        }
        const BrushRuntimeStateLayout &GetRuntimeState() const {
            return m_runtimeState;
        }

      private:
        BrushRuntimeStateLayout m_runtimeState; // 0x50
    };

    static_assert(sizeof(CBrush) == 0x100,
                  "CBrush size mismatch");
} // namespace SDK
