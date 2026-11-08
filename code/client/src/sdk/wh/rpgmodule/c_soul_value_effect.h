#pragma once

#include "c_soul_effect.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * `C_SoulValueEffect<E_SoulState, float>`, the one instantiation the RTTI
     * shows: an effect that moves one soul state by a delta.
     *
     * Layout from `C_SoulStateEffect`'s factory (0x1819A21A4) and
     * `C_HealthValueEffect`'s constructor (0x180C66228), which both write the
     * same four fields here. The shared `Apply` body (0x1814407F4) reads the
     * live state, adds `m_delta` and writes it back with the effect as its
     * cause.
     */
    template <typename State, typename Value>
    class C_SoulValueEffect: public C_SoulEffect {
      public:
        State GetState() const {
            return m_state;
        }

        Value GetDelta() const {
            return m_delta;
        }

      private:
        State m_state; // 0x18
        Value m_delta; // 0x1C
        void *m_ptr0020;              // 0x20; C_HealthValueEffect ctor 0x180C66228 writes 0
        std::uint8_t m_flag0028;      // 0x28; same ctor writes 0
        std::byte m_padding0029[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_SoulValueEffect, m_state) == 0x18, "C_SoulValueEffect::m_state offset mismatch");
            static_assert(offsetof(C_SoulValueEffect, m_delta) == 0x1C, "C_SoulValueEffect::m_delta offset mismatch");
            static_assert(offsetof(C_SoulValueEffect, m_flag0028) == 0x28, "C_SoulValueEffect::m_flag0028 offset mismatch");
        }
    };
} // namespace SDK::wh::rpgmodule
