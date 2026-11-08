#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::animationmodule {
    /**
     * Process-camera state bound to one actor. RTTI has no base classes;
     * factory 0x1827D2998 allocates 0x40 bytes and stores the actor at +0x08.
     * The native virtual destructor (0x1827D2E64 -> 0x1827D2B78) releases
     * all four bone cameras and their character references using game code.
     * Destroy this while its actor and character still exist.
     */
    class C_BoneCameraSelector {
      public:
        virtual ~C_BoneCameraSelector() = default;

      private:
        SDK::EntityId m_actorId; // 0x08
        std::byte m_padding000C[0x4]; // 0x0C
        void *m_pBoneCameras[4];      // 0x10, zeroed by the factory 0x1827D2998; the four bone cameras the destructor releases
        std::int32_t m_selected0030;  // 0x30, factory sets -1
        std::int32_t m_count0034;     // 0x34, factory sets 4
        std::uint8_t m_flag0038;      // 0x38, factory sets 0
        std::byte m_padding0039[0x7]; // 0x39

        static void AssertLayout() {
            static_assert(offsetof(C_BoneCameraSelector, m_actorId) == 0x08);
            static_assert(offsetof(C_BoneCameraSelector, m_pBoneCameras) == 0x10);
            static_assert(offsetof(C_BoneCameraSelector, m_selected0030) == 0x30);
            static_assert(offsetof(C_BoneCameraSelector, m_flag0038) == 0x38);
        }
    };

    static_assert(sizeof(C_BoneCameraSelector) == 0x40);
} // namespace SDK::wh::animationmodule
