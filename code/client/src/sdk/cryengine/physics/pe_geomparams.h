#pragma once

#include "../../types.h"

#include <bit>

namespace SDK {
    /**
     * Geometry passed to IStatObj::Physicalize. KCD2's constructor at
     * 0x18054C190 initializes this 0x70-byte layout; CPhysicalProxy's slot
     * builder (0x18041F784) supplies density, mass and the local matrix.
     */
    struct pe_geomparams {
        std::int32_t type  = 0;
        float density     = 0.0F;
        float mass        = 0.0F;
        Vec3 pos {};
        Quat q {0.0F, 0.0F, 0.0F, 1.0F};
        float scale = 1.0F;
        std::byte m_opaque002C[4] {};
        Matrix34 *pMtx3x4           = nullptr;
        Matrix33 *pMtx3x3           = nullptr;
        std::int32_t surface_idx    = INT32_MIN;
        std::uint32_t flags         = 0x18FFF;
        std::uint32_t flagsCollider = 1;
        float minContactDist       = std::bit_cast<float>(0xFFBFFFFFU);
        std::int32_t idmatBreakable = INT32_MIN;
        std::byte m_opaque0054[12] {}; // alignment and optional ITetrLattice*
        int *pMatMapping         = nullptr;
        std::int32_t nMats       = 0;
        std::int32_t bRecalcBBox = 1;

      private:
        static void AssertLayout() {
            static_assert(offsetof(pe_geomparams, type) == 0x00);
            static_assert(offsetof(pe_geomparams, density) == 0x04);
            static_assert(offsetof(pe_geomparams, mass) == 0x08);
            static_assert(offsetof(pe_geomparams, pos) == 0x0C);
            static_assert(offsetof(pe_geomparams, q) == 0x18);
            static_assert(offsetof(pe_geomparams, scale) == 0x28);
            static_assert(offsetof(pe_geomparams, pMtx3x4) == 0x30);
            static_assert(offsetof(pe_geomparams, pMtx3x3) == 0x38);
            static_assert(offsetof(pe_geomparams, surface_idx) == 0x40);
            static_assert(offsetof(pe_geomparams, flags) == 0x44);
            static_assert(offsetof(pe_geomparams, flagsCollider) == 0x48);
            static_assert(offsetof(pe_geomparams, minContactDist) == 0x4C);
            static_assert(offsetof(pe_geomparams, idmatBreakable) == 0x50);
            static_assert(offsetof(pe_geomparams, pMatMapping) == 0x60);
            static_assert(offsetof(pe_geomparams, nMats) == 0x68);
            static_assert(offsetof(pe_geomparams, bRecalcBBox) == 0x6C);
        }
    };

    static_assert(sizeof(pe_geomparams) == 0x70);
} // namespace SDK
