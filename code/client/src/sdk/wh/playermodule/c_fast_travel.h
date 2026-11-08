#pragma once

#include "c_player_module.h"
#include "e_travel_start_result.h"

#include "../../types.h"
#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * The local player's fast-travel controller, mapped read-only: starting a
     * travel moves the player and advances the world clock, which a session
     * has to arbitrate.
     *
     * The size is not asserted; the object reaches past +0x98, where the route
     * vector ValidateTravelStart measures lives.
     */
    class C_FastTravel final {
      public:
        C_FastTravel()                                = delete;
        ~C_FastTravel()                               = delete;
        C_FastTravel(const C_FastTravel &)            = delete;
        C_FastTravel &operator=(const C_FastTravel &) = delete;

        /** Null until the player module is up. */
        static C_FastTravel *GetInstance();

        bool IsTraveling() const {
            return m_traveling != 0;
        }

        /** Interpolated along the route; stale unless IsTraveling. */
        const Vec3 &GetCurrentTravelPosition() const {
            return m_currentTravelPosition;
        }

        /**
         * Whether a travel could start, and when it could not, why. Starts
         * nothing. It is the gate the map screen, the travel cutscene and
         * StartTravel all ask.
         */
        E_TravelStartResult ValidateTravelStart() const;

      private:
        std::byte m_vftable0000[0x8];               // 0x00; C_FastTravel vftable (ctor 0x180BE9BA0).
        SDK::wh::shared::C_Signal m_signal0008;      // 0x08; dispatch unk_185665430.
        SDK::wh::shared::C_Signal m_signal0018;      // 0x18; dispatch unk_185665430.
        SDK::wh::shared::C_Signal m_signal0028;      // 0x28; dispatch unk_185669B60.
        void *m_ptr0038;                             // 0x38; ctor 0.
        void *m_ptr0040;                             // 0x40; ctor 0.
        std::uint8_t m_traveling; // 0x48
        std::uint8_t m_flags0049;                    // 0x49; ctor clears the low three bits.
        std::uint8_t m_value004A;                    // 0x4A; ctor 0.
        std::byte m_padding004B[0x1];
        std::byte m_value004C[0x8];                  // 0x4C; unaligned qword, ctor 0.
        std::byte m_padding0054[0x4];
        void *m_ptr0058;                             // 0x58; ctor 0.
        void *m_ptr0060;                             // 0x60; ctor 0.
        float m_values0068[3];                       // 0x68; ctor zeroes.
        Vec3 m_currentTravelPosition; // 0x74
        std::byte m_opaque0080[0x10];
        void *m_ptr0090;                             // 0x90; ctor 0.
        void *m_ptr0098;                             // 0x98; ctor 0.
        void *m_ptr00A0;                             // 0xA0; ctor 0.
        std::byte m_object00A8[0x58];                // 0xA8; built by sub_1804C6180.
        std::uint32_t m_value0100;                   // 0x100; ctor 0.
        std::byte m_padding0104[0x4];
        bool m_flag0108;                             // 0x108; ctor 0.
        std::byte m_padding0109[0x7];
        void *m_ptr0110;                             // 0x110; ctor 0.
        void *m_ptr0118;                             // 0x118; ctor 0.
        void *m_ptr0120;                             // 0x120; ctor 0.
        void *m_ptr0128;                             // 0x128; ctor 0.

        static void AssertLayout() {
            static_assert(offsetof(C_FastTravel, m_traveling) == 0x48, "C_FastTravel::m_traveling offset mismatch");
            static_assert(offsetof(C_FastTravel, m_currentTravelPosition) == 0x74, "C_FastTravel::m_currentTravelPosition offset mismatch");
            static_assert(offsetof(C_FastTravel, m_signal0028) == 0x28, "C_FastTravel::m_signal0028 offset mismatch");
            static_assert(offsetof(C_FastTravel, m_values0068) == 0x68, "C_FastTravel::m_values0068 offset mismatch");
            static_assert(offsetof(C_FastTravel, m_object00A8) == 0xA8, "C_FastTravel::m_object00A8 offset mismatch");
            static_assert(offsetof(C_FastTravel, m_ptr0128) == 0x128, "C_FastTravel::m_ptr0128 offset mismatch");
        }
    };

    // C_PlayerModule ctor 0x180BE9208 allocates 0x130.
    static_assert(sizeof(C_FastTravel) == 0x130, "C_FastTravel size mismatch");
} // namespace SDK::wh::playermodule
