#pragma once

#include "i_render_node.h"

#include <cstdint>

namespace SDK {
    struct pe_params_area;

    /** Spawn/configuration interface for oceans, bounded water, and rivers. */
    class IWaterVolumeRenderNode : public IRenderNode {
      public:
        enum EWaterVolumeType : std::int32_t {
            eWVT_Unknown,
            eWVT_Ocean,
            eWVT_Area,
            eWVT_River,
        };

        ~IWaterVolumeRenderNode() override = default;

        virtual void SetAreaAttachedToEntity() = 0; // +0x248
        virtual void SetFogDensity(float fogDensity) = 0; // +0x250
        virtual float GetFogDensity() const = 0; // +0x258
        virtual void SetFogColor(const Vec3 &fogColor) = 0; // +0x260
        virtual void SetFogColorAffectedBySun(bool enable) = 0; // +0x268
        virtual void SetFogShadowing(float fogShadowing) = 0; // +0x270
        virtual void SetCapFogAtVolumeDepth(bool capFog) = 0; // +0x278
        virtual void SetVolumeDepth(float volumeDepth) = 0; // +0x280
        virtual void SetStreamSpeed(float streamSpeed) = 0; // +0x288
        virtual void SetCaustics(bool caustics) = 0; // +0x290
        virtual void SetCausticIntensity(float intensity) = 0; // +0x298
        virtual void SetCausticTiling(float tiling) = 0; // +0x2A0
        virtual void SetCausticHeight(float height) = 0; // +0x2A8
        virtual void SetPhysParams(float density, float resistance) = 0; // +0x2B0
        virtual void SetAuxPhysParams(pe_params_area *params) = 0; // +0x2B8
        virtual void CreateOcean(std::uint64_t volumeId,
                                 bool keepSerializationParams = false) = 0; // +0x2C0
        virtual void CreateArea(std::uint64_t volumeId, const Vec3 *vertices,
                                std::uint32_t vertexCount, const Vec2 &uvScale,
                                const Plane &fogPlane,
                                bool keepSerializationParams = false) = 0; // +0x2C8
        virtual void CreateRiver(std::uint64_t volumeId, const Vec3 *vertices,
                                 std::uint32_t vertexCount, float texCoordBegin,
                                 float texCoordEnd, const Vec2 &uvScale,
                                 const Plane &fogPlane,
                                 bool keepSerializationParams = false) = 0; // +0x2D0
        virtual void SetAreaPhysicsArea(const Vec3 *vertices,
                                        std::uint32_t vertexCount,
                                        bool keepSerializationParams = false) = 0; // +0x2D8
        virtual void SetRiverPhysicsArea(const Vec3 *vertices,
                                         std::uint32_t vertexCount,
                                         bool keepSerializationParams = false) = 0; // +0x2E0
        virtual IPhysicalEntity *SetAndCreatePhysicsArea(
            const Vec3 *vertices, std::uint32_t vertexCount) = 0; // +0x2E8
    };

    static_assert(sizeof(IWaterVolumeRenderNode) == 0x50,
                  "IWaterVolumeRenderNode size mismatch");
} // namespace SDK
