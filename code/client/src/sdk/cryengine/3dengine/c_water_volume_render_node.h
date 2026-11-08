#pragma once

#include "cry_3d_engine_base.h"
#include "i_water_volume_render_node.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct IMaterial;
    struct CREWaterVolume;
    struct SWaterVolumeSerialize;
    struct SWaterVolumePhysAreaInput;

    struct WaterVolumeRenderParamsLayout {
        std::byte opaque[0x80];
    };

    template <typename T>
    struct WaterVolumeVectorLayout {
        T *begin;
        T *end;
        T *capacityEnd;
    };

    struct WaterSurfaceVertexLayout {
        std::byte opaque[0x18];
    };

    struct WaterVolumeAuxPhysParamsLayout {
        std::byte opaque[0x68];
    };

    /** Constructor/destructor-proven state following IWaterVolumeRenderNode. */
    struct WaterVolumeRuntimeStateLayout {
        IWaterVolumeRenderNode::EWaterVolumeType volumeType; // 0x000
        std::uint32_t padding0004;
        std::uint64_t volumeId;                              // 0x008
        float volumeDepth;                                   // 0x010
        float streamSpeed;                                   // 0x014
        WaterVolumeRenderParamsLayout renderParams[2];       // 0x018
        IMaterial *material;                                 // 0x118
        IMaterial *waterBodyIntoMaterial;                    // 0x120
        IMaterial *waterBodyOutOfMaterial;                   // 0x128
        CREWaterVolume *volumeRenderElements[2];             // 0x130
        CREWaterVolume *surfaceRenderElements[2];            // 0x140
        SWaterVolumeSerialize *serializationParams;          // 0x150
        SWaterVolumePhysAreaInput *physicsAreaInput;          // 0x158
        IPhysicalEntity *physicalArea;                       // 0x160
        WaterVolumeVectorLayout<WaterSurfaceVertexLayout> surfaceVertices; // 0x168
        WaterVolumeVectorLayout<std::uint16_t> surfaceIndices; // 0x180
        Matrix34 parentEntityWorldTransform;                  // 0x198
        std::uint16_t layerId;                               // 0x1C8
        std::byte padding01CA[0x2];
        float fogDensity;                                    // 0x1CC
        Vec3 fogColor;                                       // 0x1D0
        bool fogColorAffectedBySun;                          // 0x1DC
        std::byte padding01DD[0x3];
        float fogShadowing;                                  // 0x1E0
        Plane fogPlane;                                      // 0x1E4
        Plane baseFogPlane;                                  // 0x1F4
        Vec3 offset;                                         // 0x204
        Vec3 center;                                         // 0x210
        AABB worldBounds;                                    // 0x21C
        bool capFogAtVolumeDepth;                            // 0x234
        bool attachedToEntity;                               // 0x235
        bool caustics;                                       // 0x236
        std::byte padding0237;
        float causticIntensity;                              // 0x238
        float causticTiling;                                 // 0x23C
        float causticShadow;                                 // 0x240
        float causticHeight;                                 // 0x244
        WaterVolumeAuxPhysParamsLayout auxiliaryPhysics;     // 0x248
        float density;                                       // 0x2B0
        float resistance;                                    // 0x2B4
    };

    /** Concrete product of CreateRenderNode(eERType_WaterVolume). */
    class CWaterVolumeRenderNode : public IWaterVolumeRenderNode,
                                   public Cry3DEngineBase {
      public:
        ~CWaterVolumeRenderNode() override = default;

        EWaterVolumeType GetVolumeType() const { return m_state.volumeType; }
        std::uint64_t GetVolumeId() const { return m_state.volumeId; }
        float GetVolumeDepthRaw() const { return m_state.volumeDepth; }
        float GetStreamSpeedRaw() const { return m_state.streamSpeed; }
        IPhysicalEntity *GetPhysicalArea() const { return m_state.physicalArea; }
        const Matrix34 &GetParentEntityWorldTransform() const {
            return m_state.parentEntityWorldTransform;
        }
        const AABB &GetWorldBounds() const { return m_state.worldBounds; }
        bool IsAttachedToEntity() const { return m_state.attachedToEntity; }
        bool HasCaustics() const { return m_state.caustics; }
        const WaterVolumeRuntimeStateLayout &GetRuntimeState() const {
            return m_state;
        }

      private:
        WaterVolumeRuntimeStateLayout m_state; // 0x050
    };

    static_assert(sizeof(WaterVolumeRenderParamsLayout) == 0x80,
                  "WaterVolumeRenderParamsLayout size mismatch");
    static_assert(sizeof(WaterVolumeVectorLayout<std::uint16_t>) == 0x18,
                  "WaterVolumeVectorLayout size mismatch");
    static_assert(sizeof(WaterVolumeAuxPhysParamsLayout) == 0x68,
                  "WaterVolumeAuxPhysParamsLayout size mismatch");
    static_assert(offsetof(WaterVolumeRuntimeStateLayout, surfaceVertices) == 0x168,
                  "water surface-vertex vector offset mismatch");
    static_assert(offsetof(WaterVolumeRuntimeStateLayout, fogDensity) == 0x1CC,
                  "water fog-density offset mismatch");
    static_assert(offsetof(WaterVolumeRuntimeStateLayout, worldBounds) == 0x21C,
                  "water world-bounds offset mismatch");
    static_assert(offsetof(WaterVolumeRuntimeStateLayout, auxiliaryPhysics) == 0x248,
                  "water auxiliary-physics offset mismatch");
    static_assert(sizeof(WaterVolumeRuntimeStateLayout) == 0x2B8,
                  "WaterVolumeRuntimeStateLayout size mismatch");
    static_assert(sizeof(CWaterVolumeRenderNode) == 0x308,
                  "CWaterVolumeRenderNode size mismatch");
} // namespace SDK
