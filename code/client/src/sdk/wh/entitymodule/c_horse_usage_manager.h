#pragma once

#include "../framework/c_override_conflict_manager.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    struct HorseUsageOverrideEntryLayout {
        wh::framework::WUID m_key;
        wh::framework::WUID m_value;
    };

    static_assert(sizeof(HorseUsageOverrideEntryLayout) == 0x10,
                  "HorseUsageOverrideEntryLayout size mismatch");

    /**
     * Runtime manager for horse-usage ownership/conflicts.
     *
     * RTTI proves the namespace and hierarchy through
     * C_OverrideConflictManager<WUID, WUID>. Its table at 0x183EAAE40
     * implements exactly the three inherited entries and adds no operations.
     * Default construction and sized deletion prove the 0x80-byte layout.
     */
    class C_HorseUsageManager
        : public wh::framework::C_OverrideConflictManager<wh::framework::WUID,
                                                          wh::framework::WUID> {
      public:
        ~C_HorseUsageManager() override = default;

        std::size_t GetOverrideEntryCount() const {
            return static_cast<std::size_t>(m_overrideEntryCount);
        }

        const HorseUsageOverrideEntryLayout *GetOverrideEntries() const {
            return m_pOverrideEntries;
        }

      private:
        HorseUsageOverrideEntryLayout *m_pOverrideEntries; // 0x18
        std::uint64_t m_overrideEntryCount;                 // 0x20
        std::uint64_t m_overrideEntryCapacity;              // 0x28
        std::uint32_t m_state0030; // initialized to 2
        std::byte m_padding0034[0x4];
        wh::framework::WUID m_identifier0038;
        wh::framework::WUID m_identifier0040;
        wh::framework::WUID m_state0048[6];
        std::int32_t m_state0078; // initialized to -1
        std::uint32_t m_state007C;
    };

    static_assert(sizeof(C_HorseUsageManager) == 0x80,
                  "C_HorseUsageManager size mismatch");
} // namespace SDK::wh::entitymodule
