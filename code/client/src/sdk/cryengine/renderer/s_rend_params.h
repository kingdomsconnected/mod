#pragma once

#include "../../types.h"
#include "../3dengine/i_fog_volume_render_node.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Per-draw parameters an object is submitted with: CryEngine's
     * `SRendParams`, KCD2's 0xBC-byte
     * copy, laid out from its copy constructor (0x1804A0084).
     *
     * KCD2 matches 5.7 except at +0x80 and +0x88, two owning members the
     * copy constructor add-refs and the destructor releases -- so a
     * parameter block the mod builds keeps both null, and it is never copied.
     * `CEntitySlotObject::Render` (0x18049F08C) is the worked example: the
     * slot's world matrix in `pMatrix`, 0x2000000 ORed into `objectFlags`.
     */
    struct SRendParams {
        /** What `CEntitySlotObject::Render` ORs into every slot draw; its meaning is not established. */
        static constexpr std::uint64_t kSlotObjectFlags = 0x2000000;

        const Matrix34 *pMatrix        = nullptr; // 0x00
        const Matrix34 *pPrevMatrix    = nullptr; // 0x08, motion vectors; the same matrix for a still object
        const Matrix34 *pNearestMatrix = nullptr; // 0x10
        void *pVisArea                 = nullptr; // 0x18
        void *pMaterial                = nullptr; // 0x20, an override; null keeps the object's own
        void *pFoliage                 = nullptr; // 0x28
        void *pRenderNode              = nullptr; // 0x30
        void *pInstance                = nullptr; // 0x38
        void *pTerrainTexInfo          = nullptr; // 0x40
        void *pShaderParams            = nullptr; // 0x48
        ColorF ambientColor            = {};      // 0x50
        float alpha                    = 1.0F;    // 0x60
        float distance                 = 0.0F;    // 0x64
        std::uint64_t objectFlags      = 0;       // 0x68
        std::uint32_t unknown0070[3]   = {};      // 0x70
        std::uint32_t padding007C      = 0;       // 0x7C
        void *ownedReference0080       = nullptr; // 0x80, add-ref'd by the copy constructor: keep null
        void *ownedArray0088[2]        = {};      // 0x88, a shared array the destructor frees: keep null
        float customData[4]            = {};      // 0x98
        std::uint16_t textureId        = 0;       // 0xA8
        std::uint16_t customFlags      = 0;       // 0xAA
        std::uint32_t lodValue         = 0;       // 0xAC
        std::uint8_t unknown00B0[7]    = {};      // 0xB0
        std::uint8_t afterWater        = 1;       // 0xB7
        std::uint32_t unknown00B8      = 0;       // 0xB8
        std::uint32_t padding00BC      = 0;       // 0xBC
    };

    static_assert(offsetof(SRendParams, ambientColor) == 0x50, "SRendParams::ambientColor offset mismatch");
    static_assert(offsetof(SRendParams, alpha) == 0x60, "SRendParams::alpha offset mismatch");
    static_assert(offsetof(SRendParams, objectFlags) == 0x68, "SRendParams::objectFlags offset mismatch");
    static_assert(offsetof(SRendParams, ownedReference0080) == 0x80, "SRendParams::ownedReference0080 offset mismatch");
    static_assert(offsetof(SRendParams, customData) == 0x98, "SRendParams::customData offset mismatch");
    static_assert(offsetof(SRendParams, lodValue) == 0xAC, "SRendParams::lodValue offset mismatch");
    static_assert(offsetof(SRendParams, afterWater) == 0xB7, "SRendParams::afterWater offset mismatch");
    static_assert(sizeof(SRendParams) == 0xC0, "SRendParams size mismatch");
} // namespace SDK
