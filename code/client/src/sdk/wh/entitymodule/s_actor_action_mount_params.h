#pragma once

#include "../../types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Native mount parameters, named by C_ActorActionMount's RTTI.
     * Construction at 0x180A9D930 and assignment at 0x180A9D7D4 prove the
     * layout. The game owns the string and completion callable; they must
     * never be copied to another process or destroyed by the mod.
     */
    struct alignas(8) S_ActorActionMountParams {
        const SDK::QuatT &GetHorseTransform() const {
            return m_horseTransform;
        }
        const SDK::QuatT &GetRiderAlignment() const {
            return m_riderAlignment;
        }
        std::uint32_t GetRiderFragment() const {
            return m_riderFragment;
        }
        std::uint32_t GetHorseFragment() const {
            return m_horseFragment;
        }
        const std::array<std::uint8_t, 20> &GetRiderTags() const {
            return m_riderTags;
        }
        const std::array<std::uint8_t, 20> &GetHorseTags() const {
            return m_horseTags;
        }


      private:
        std::byte m_opaque0000[0x10];             // game string, flags and alignment
        SDK::QuatT m_horseTransform;              // 0x10
        SDK::QuatT m_riderAlignment;              // 0x2C, inverse animation-root transform
        std::uint32_t m_riderFragment;            // 0x48
        std::array<std::uint8_t, 20> m_riderTags; // 0x4C
        std::uint32_t m_horseFragment;            // 0x60
        std::array<std::uint8_t, 20> m_horseTags; // 0x64
        std::byte m_opaque0078[0x40];             // native completion callable

        static void AssertLayout() {
            static_assert(offsetof(S_ActorActionMountParams, m_horseTransform) == 0x10);
            static_assert(offsetof(S_ActorActionMountParams, m_riderAlignment) == 0x2C);
            static_assert(offsetof(S_ActorActionMountParams, m_riderFragment) == 0x48);
            static_assert(offsetof(S_ActorActionMountParams, m_riderTags) == 0x4C);
            static_assert(offsetof(S_ActorActionMountParams, m_horseFragment) == 0x60);
            static_assert(offsetof(S_ActorActionMountParams, m_horseTags) == 0x64);
        }
    };

    static_assert(sizeof(S_ActorActionMountParams) == 0xB8);
} // namespace SDK::wh::entitymodule
