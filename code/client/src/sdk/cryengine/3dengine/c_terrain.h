#pragma once

#include "c_ocean.h"
#include "i_terrain.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryEngine terrain implementation.
     *
     * CTerrain's table at 0x1840404D8 implements all twenty ITerrain entries
     * and appends three concrete operations. Its factory allocation proves the
     * complete 0x258-byte size. The added contracts remain opaque because
     * their source signatures are absent from the binary.
     */
    class CTerrain : public ITerrain {
      public:
        ~CTerrain() override = default;

        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0; // +0xB0

        /** Returns the concrete terrain used by the process-wide 3D engine. */
        static CTerrain *GetInstance();

        // The ocean the terrain owns (m_pOcean, stock CTerrain order).
        COcean *GetOcean() { return m_pOcean; }
        const COcean *GetOcean() const { return m_pOcean; }

      private:
        // Constructor 0x18095A16C. Only the members it pins to a stock name
        // are named: the ocean, the three texture caches it builds with a
        // vector constructor iterator, and the update dispatcher it allocates
        // last. The rest is zeroed or copied from the level's terrain info.
        std::int32_t m_state0008;                 // 0x008, copied from C3DEngine+0xBAC
        std::int32_t m_state000C;                 // 0x00C, copied from C3DEngine+0xBA4
        std::byte m_opaque0010[0xD8];             // 0x010
        COcean *m_pOcean;                         // 0x0E8
        std::byte m_opaque00F0[0x38];             // 0x0F0
        std::byte m_texCache[3][0x58];            // 0x128, CTextureCache[3] (RGB, normal, height), constructed by 0x18194B370
        bool m_state0230;                         // 0x230
        std::byte m_padding0231[0x7];
        void *m_pTerrainUpdateDispatcher;         // 0x238, CTerrainUpdateDispatcher *, 0x60 bytes built by 0x18095A06C
        std::byte m_opaque0240[0x18];             // 0x240, zeroed

        static void AssertLayout() {
            static_assert(offsetof(CTerrain, m_pOcean) == 0xE8, "CTerrain::m_pOcean offset mismatch");
            static_assert(offsetof(CTerrain, m_texCache) == 0x128, "CTerrain::m_texCache offset mismatch");
            static_assert(offsetof(CTerrain, m_pTerrainUpdateDispatcher) == 0x238, "CTerrain::m_pTerrainUpdateDispatcher offset mismatch");
        }
    };

    static_assert(sizeof(CTerrain) == 0x258, "CTerrain size mismatch");
} // namespace SDK
