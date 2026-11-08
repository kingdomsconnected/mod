#pragma once

#include "cry_3d_engine_base.h"
#include "i_vegetation.h"
#include "../../types.h"
#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Exact constructor and terrain-spawn-proven CVegetation state at +0x50. */
    struct VegetationRuntimeStateLayout {
        Vec3 m_position; // 0x00
        bool m_registered; // 0x0C, cleared during destruction
        std::byte m_padding000D[0x3];
        void *m_pState0010;
        void *m_pState0018;
        void *m_pBoundsState; // 0x20
        std::uint32_t m_staticGroupId; // 0x28
        std::uint8_t m_angle; // 0x2C
        std::uint8_t m_encodedScale; // 0x2D, scale * 64
        std::uint8_t m_boundsState[0x7]; // 0x2E
        std::uint8_t m_angleX; // 0x35
        std::uint8_t m_angleY; // 0x36
        std::uint8_t m_flags; // 0x37

        float GetStoredScale() const {
            return static_cast<float>(m_encodedScale) * (1.0F / 64.0F);
        }
    };

    static_assert(sizeof(VegetationRuntimeStateLayout) == 0x38,
                  "VegetationRuntimeStateLayout size mismatch");

    /**
     * Concrete vegetation render node spawned by CTerrain.
     *
     * RTTI proves IVegetation at +0x00 and the empty Cry3DEngineBase tag at
     * +0x50. The 74-slot table adds nothing beyond IVegetation. Default
     * construction initializes fields through +0x87, and the fixed render-node
     * pool allocates one 0x88-byte object for each vegetation instance.
     */
    class CVegetation : public IVegetation, public Cry3DEngineBase {
      public:
        ~CVegetation() override = default;

        const Vec3 &GetSpawnPosition() const { return m_runtimeState.m_position; }
        std::uint32_t GetStaticGroupId() const {
            return m_runtimeState.m_staticGroupId;
        }
        void SetScale(float scale) {
            hook::this_call<void>(gPatterns.CVegetation__SetScale, this, scale);
        }
        const VegetationRuntimeStateLayout &GetRuntimeState() const {
            return m_runtimeState;
        }

      private:
        VegetationRuntimeStateLayout m_runtimeState; // 0x50
    };

    static_assert(sizeof(CVegetation) == 0x88,
                  "CVegetation size mismatch");
} // namespace SDK
