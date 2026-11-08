#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Shared actor material-effects runtime.
     *
     * Its standalone RTTI table at 0x183E74088 contains the virtual
     * destructor followed by exactly three material-effect operations. The
     * following string data proves the table boundary, and scalar deletion
     * proves the exact 0xB0-byte layout.
     */
    class C_ActorMaterialEffects {
      public:
        virtual ~C_ActorMaterialEffects() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;

      private:
        // Ctor 0x180BC89CC, destructor 0x1819D09FC.
        std::uint8_t m_enabled0008;            // 0x08, ctor zeroes; destructor clears it first
        std::uint8_t m_state0009;              // 0x09, ctor zeroes
        std::byte m_padding000A[0x6];
        SDK::CryStringT<char> m_library0010;   // 0x10, ctor assigns the empty string
        StdVector<std::byte> m_pending0018;    // 0x18, std::vector<std::pair<int, std::function<long()>>>; element type kept opaque
        std::uint32_t m_value0030;             // 0x30, ctor sets 2
        std::byte m_padding0034[0x4];
        void *m_values0038[2];                 // 0x38, ctor zeroes
        std::byte m_zeroed0048[0x30];          // 0x48, three 16-byte stores of zero in the ctor
        std::int32_t m_value0078;              // 0x78, ctor sets -1
        std::uint32_t m_value007C;             // 0x7C, ctor zeroes
        StdVector<std::byte> m_pending0080;    // 0x80, std::vector<std::pair<int, std::function<long()>>>; element type kept opaque
        std::byte m_container0098[0x18];       // 0x98, destroyed by 0x180A3B9C4

        static void AssertLayout() {
            static_assert(offsetof(C_ActorMaterialEffects, m_library0010) == 0x10, "C_ActorMaterialEffects::m_library0010 offset mismatch");
            static_assert(offsetof(C_ActorMaterialEffects, m_value0030) == 0x30, "C_ActorMaterialEffects::m_value0030 offset mismatch");
            static_assert(offsetof(C_ActorMaterialEffects, m_value0078) == 0x78, "C_ActorMaterialEffects::m_value0078 offset mismatch");
            static_assert(offsetof(C_ActorMaterialEffects, m_pending0080) == 0x80, "C_ActorMaterialEffects::m_pending0080 offset mismatch");
            static_assert(offsetof(C_ActorMaterialEffects, m_container0098) == 0x98, "C_ActorMaterialEffects::m_container0098 offset mismatch");
        }
    };

    static_assert(sizeof(C_ActorMaterialEffects) == 0xB0,
                  "C_ActorMaterialEffects size mismatch");
} // namespace SDK::wh::entitymodule
