#pragma once

#include "c_action_actor.h"
#include "c_actor_action_item_handling_anim.h"
#include "i_alignment_manager.h"
#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The alignment solvers of one actor.
     *
     * Built by the actor's action queue (constructor 0x1809CDB7C, 0x1B8 bytes
     * allocated at 0x1809CDF14) and stored at `C_ActionActor+0x58`; the
     * `C_ActorObject` base holds that queue. Each solver kind has one slot,
     * named here from the manager's own debug printer (0x1828C5E04): ground,
     * rotation and position.
     */
    class C_AlignmentManager
        : public C_ActorObject
        , public I_AlignmentManager {
      public:
        /** The body these solvers move. */
        C_Actor *GetOwner() const {
            return GetActionActor()->GetActor();
        }

      private:
        // Ctor 0x1809CDB7C plus its reset 0x1809CE264 initialise the members below.
        std::uint8_t m_flag0018;           // 0x18, reset sets 1
        std::byte m_padding0019[0x7];      // 0x19
        SDK::CryStringT<char> m_name0020;  // 0x20, reset assigns the empty string
        std::uint32_t m_value0028;         // 0x28, reset zeroes
        std::byte m_opaque002C[0x38];      // 0x2C
        std::uint8_t m_flag0064;           // 0x64, reset zeroes
        std::uint8_t m_flag0065;           // 0x65, ctor sets 1
        std::byte m_padding0066[0x2];      // 0x66
        void *m_pGroundSolver;             // 0x68
        void *m_pRotationSolver;           // 0x70
        void *m_pPositionSolver;           // 0x78
        void *m_pOwned0080;                // 0x80, dtor 0x180FC1C48 deletes it through vtable slot 0
        std::byte m_callback0088[0x40];    // 0x88, native std::function (impl at +0x38, destroyed by 0x180399FBC)
        std::byte m_callback00C8[0x40];    // 0xC8, native std::function (impl at +0x38, destroyed by 0x180399FBC)
        std::byte m_opaque0108[0x8];       // 0x108
        std::byte m_state0110[0x68];       // 0x110, built by 0x1809CDD94; native std::function<float()> at +0x10
        void *m_value0178;                 // 0x178, ctor zeroes
        std::byte m_opaque0180[0x1C];      // 0x180
        float m_values019C[4];             // 0x19C, ctor zeroes
        float m_values01AC[3];             // 0x1AC, ctor zeroes

        static void AssertLayout() {
            static_assert(offsetof(C_AlignmentManager, m_pGroundSolver) == 0x68, "C_AlignmentManager::m_pGroundSolver offset mismatch");
            static_assert(offsetof(C_AlignmentManager, m_pRotationSolver) == 0x70, "C_AlignmentManager::m_pRotationSolver offset mismatch");
            static_assert(offsetof(C_AlignmentManager, m_pPositionSolver) == 0x78, "C_AlignmentManager::m_pPositionSolver offset mismatch");
            static_assert(offsetof(C_AlignmentManager, m_name0020) == 0x20, "C_AlignmentManager::m_name0020 offset mismatch");
            static_assert(offsetof(C_AlignmentManager, m_flag0065) == 0x65, "C_AlignmentManager::m_flag0065 offset mismatch");
            static_assert(offsetof(C_AlignmentManager, m_callback0088) == 0x88, "C_AlignmentManager::m_callback0088 offset mismatch");
            static_assert(offsetof(C_AlignmentManager, m_state0110) == 0x110, "C_AlignmentManager::m_state0110 offset mismatch");
            static_assert(offsetof(C_AlignmentManager, m_values019C) == 0x19C, "C_AlignmentManager::m_values019C offset mismatch");
        }
    };

    static_assert(sizeof(C_AlignmentManager) == 0x1B8, "C_AlignmentManager size mismatch");
} // namespace SDK::wh::entitymodule
