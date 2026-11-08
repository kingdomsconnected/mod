#pragma once

#include "../../types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Native dismount parameters. Construction at 0x1817D0718 and the ride
     * context's builder at 0x180A52378 establish this layout. Its completion
     * callable remains owned and populated by the native queueing operation.
     */
    struct alignas(8) S_ActorActionDismountParams {
        const SDK::QuatT &GetPlacement() const {
            return m_placement;
        }
        bool ShouldPlace() const {
            return m_place;
        }
        std::uint8_t GetContextFlag() const {
            return m_contextFlag;
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
        SDK::QuatT m_placement; // 0x00
        bool m_place;           // 0x1C, context's place-on-dismount flag
        std::byte m_opaque001D[3];
        std::uint8_t m_contextFlag; // 0x20, copied from ride context +0x0A
        std::byte m_opaque0021[3];
        SDK::EntityId m_horseEntityId;            // 0x24, the ride action's +0x50
        std::uint32_t m_riderFragment;            // 0x28
        std::array<std::uint8_t, 20> m_riderTags; // 0x2C
        std::uint32_t m_horseFragment;            // 0x40
        std::array<std::uint8_t, 20> m_horseTags; // 0x44
        std::byte m_opaque0058[0x40];             // native completion callable

        static void AssertLayout() {
            static_assert(offsetof(S_ActorActionDismountParams, m_placement) == 0x00);
            static_assert(offsetof(S_ActorActionDismountParams, m_place) == 0x1C);
            static_assert(offsetof(S_ActorActionDismountParams, m_contextFlag) == 0x20);
            static_assert(offsetof(S_ActorActionDismountParams, m_horseEntityId) == 0x24);
            static_assert(offsetof(S_ActorActionDismountParams, m_riderFragment) == 0x28);
            static_assert(offsetof(S_ActorActionDismountParams, m_riderTags) == 0x2C);
            static_assert(offsetof(S_ActorActionDismountParams, m_horseFragment) == 0x40);
            static_assert(offsetof(S_ActorActionDismountParams, m_horseTags) == 0x44);
        }
    };

    static_assert(sizeof(S_ActorActionDismountParams) == 0x98);
} // namespace SDK::wh::entitymodule
