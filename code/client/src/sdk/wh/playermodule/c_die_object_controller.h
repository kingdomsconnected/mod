#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * One visible die, separate from its scoring C_DieModel. The factory
     * at 0x182E7C8B0 allocates 0xD0 bytes including the shared control block;
     * SetEntityId (0x182E94E94) stores the entity at controller +0x30.
     */
    class C_DieObjectController {
      public:
        std::uint32_t GetEntityId() const {
            return m_entityId;
        }

      private:
        std::byte m_opaque0000[0x30];
        std::uint32_t m_entityId; // 0x30
        std::byte m_opaque0034[0x8C];

        static void AssertLayout() {
            static_assert(offsetof(C_DieObjectController, m_entityId) == 0x30, "C_DieObjectController::m_entityId offset mismatch");
        }
    };

    static_assert(sizeof(C_DieObjectController) == 0xC0, "C_DieObjectController size mismatch");
} // namespace SDK::wh::playermodule
