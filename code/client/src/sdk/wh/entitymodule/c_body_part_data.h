#pragma once

#include "../../patterns.h"
#include "../../types.h"

#include <cstddef>
#include <cstdint>
#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    /** Native TakeDamage's body-part argument; owns three native CryStrings. */
    class alignas(8) C_BodyPartData {
      public:
        C_BodyPartData() {
            hook::this_call<void>(gPatterns.C_BodyPartData__Construct, this);
        }
        ~C_BodyPartData() {
            hook::this_call<void>(gPatterns.C_BodyPartData__Destroy, this);
        }
        C_BodyPartData(const C_BodyPartData &)            = delete;
        C_BodyPartData &operator=(const C_BodyPartData &) = delete;

      private:
        // Ctor 0x180EE0404; native assignment 180EE0098 copies through the dword at +0x38.
        std::int32_t m_value0000 {};             // 0x00, ctor sets -1
        std::byte m_padding0004[0x4] {};
        SDK::CryStringT<char> m_string0008 {};   // 0x08, ctor sets the empty string
        std::int32_t m_value0010 {};             // 0x10, ctor zeroes
        std::int32_t m_value0014 {};             // 0x14, ctor sets -1
        SDK::CryStringT<char> m_string0018 {};   // 0x18, ctor sets the empty string
        std::uint32_t m_values0020[4] {};        // 0x20, ctor copies the all-zero constant at 0x18409FE00
        SDK::CryStringT<char> m_string0030 {};   // 0x30, ctor sets the empty string
        std::uint32_t m_value0038 {};            // 0x38, not set by the ctor; the assignment copies it
        std::byte m_padding003C[0x4] {};

        static void AssertLayout() {
            static_assert(offsetof(C_BodyPartData, m_string0008) == 0x08, "C_BodyPartData::m_string0008 offset mismatch");
            static_assert(offsetof(C_BodyPartData, m_value0014) == 0x14, "C_BodyPartData::m_value0014 offset mismatch");
            static_assert(offsetof(C_BodyPartData, m_string0018) == 0x18, "C_BodyPartData::m_string0018 offset mismatch");
            static_assert(offsetof(C_BodyPartData, m_values0020) == 0x20, "C_BodyPartData::m_values0020 offset mismatch");
            static_assert(offsetof(C_BodyPartData, m_string0030) == 0x30, "C_BodyPartData::m_string0030 offset mismatch");
            static_assert(offsetof(C_BodyPartData, m_value0038) == 0x38, "C_BodyPartData::m_value0038 offset mismatch");
        }
    };
    static_assert(sizeof(C_BodyPartData) == 0x40);
} // namespace SDK::wh::entitymodule
