#pragma once

#include "../../types.h"
#include "i_physical_world.h"
#include "pe_params_particle.h"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Changes one or every part of a physical entity: placement, scale,
     * collision flags, mass. CryPhysics' `pe_params_part`, type 6, 0xA0 bytes,
     * laid out as KCD2's own constructor (0x18054BE10) fills it -- every field
     * a "leave it" sentinel, so a request names only what it changes. With
     * neither `partid` nor `ipart` set it applies to every part.
     *
     * The flags are applied as `(flags & flagsAND) | flagsOR`, the collider
     * flags likewise. Lua's `SetPhysicParams(PHYSICPARAM_PART_FLAGS, ...)`
     * builds exactly this (CScriptBind physics helper 0x180B35634): its
     * `flags_mask` is `~flagsAND`, its `flags` is `flagsOR`.
     */
    struct pe_params_part: pe_params {
        static constexpr float kUnusedFloat      = std::bit_cast<float>(0xFFBFFFFFU);
        static constexpr std::int32_t kUnusedInt = INT32_MIN;

        pe_params_part(): pe_params {6} {}

        std::int32_t partid      = kUnusedInt; // 0x04
        std::int32_t ipart       = kUnusedInt; // 0x08
        std::int32_t bRecalcBBox = 1;          // 0x0C
        float pos[3] {kUnusedFloat, 0.0F, 0.0F};        // 0x10
        float q[4] {0.0F, 0.0F, 0.0F, kUnusedFloat};     // 0x1C, x y z w
        float scale                 = kUnusedFloat;      // 0x2C
        void *pMtx3x4               = nullptr;           // 0x30
        void *pMtx3x3               = nullptr;           // 0x38
        std::uint32_t flagsCond     = 0x80000000U;       // 0x40
        std::uint32_t flagsOR       = 0;                 // 0x44
        std::uint32_t flagsAND      = 0xFFFFFFFFU;       // 0x48
        std::uint32_t flagsColliderOR  = 0;              // 0x4C
        std::uint32_t flagsColliderAND = 0xFFFFFFFFU;    // 0x50
        float mass                  = kUnusedFloat;      // 0x54
        float density               = kUnusedFloat;      // 0x58
        float minContactDist        = kUnusedFloat;      // 0x5C
        std::intptr_t pPhysGeom      = -1;               // 0x60
        std::intptr_t pPhysGeomProxy = -1;               // 0x68
        std::int32_t idmatBreakable  = kUnusedInt;       // 0x70
        std::byte m_padding0074[0x4] {};
        std::intptr_t pLattice      = -1;                // 0x78
        std::int32_t idSkeleton     = kUnusedInt;        // 0x80
        std::byte m_padding0084[0x4] {};
        std::intptr_t pMatMapping   = -1;                // 0x88
        std::int32_t nMats          = 0;                 // 0x90
        float m_value0094           = kUnusedFloat;      // 0x94
        std::int32_t m_value0098    = 0;                 // 0x98
        std::int32_t m_value009C    = kUnusedInt;        // 0x9C
    };

    static_assert(sizeof(pe_params_part) == 0xA0, "pe_params_part size mismatch");
    static_assert(offsetof(pe_params_part, partid) == 0x04, "pe_params_part::partid offset mismatch");
    static_assert(offsetof(pe_params_part, flagsCond) == 0x40, "pe_params_part::flagsCond offset mismatch");
    static_assert(offsetof(pe_params_part, flagsOR) == 0x44, "pe_params_part::flagsOR offset mismatch");
    static_assert(offsetof(pe_params_part, flagsAND) == 0x48, "pe_params_part::flagsAND offset mismatch");
    static_assert(offsetof(pe_params_part, flagsColliderOR) == 0x4C, "pe_params_part::flagsColliderOR offset mismatch");
    static_assert(offsetof(pe_params_part, flagsColliderAND) == 0x50, "pe_params_part::flagsColliderAND offset mismatch");
    static_assert(offsetof(pe_params_part, idmatBreakable) == 0x70, "pe_params_part::idmatBreakable offset mismatch");
    static_assert(offsetof(pe_params_part, pMatMapping) == 0x88, "pe_params_part::pMatMapping offset mismatch");
} // namespace SDK
