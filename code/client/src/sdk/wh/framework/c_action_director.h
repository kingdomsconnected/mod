#pragma once

#include "../../types.h"
#include "../shared/c_signal.h"
#include "i_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::framework {
    /**
     * RTTI at primary table 0x183A67DE0 has no base. Constructor
     * 0x1809CDAD0 initializes through +0x88; embedded alchemy construction
     * fixes the complete 0x90-byte extent. Slot ownership is read in place.
     */
    class C_ActionDirector {
      public:
        virtual ~C_ActionDirector() = default;
        virtual void unk_director0001();

        /** Exact bounds/presence predicate at 0x1808D2450. */
        bool HasAction(std::int32_t slot) const {
            return slot >= 0 && static_cast<std::size_t>(slot) < m_actions.Size() && m_actions[static_cast<std::size_t>(slot)] != nullptr;
        }

        const SDK::wh::framework::I_Action *GetAction(std::int32_t slot) const {
            return HasAction(slot) ? m_actions[static_cast<std::size_t>(slot)] : nullptr;
        }
        SDK::wh::framework::I_Action *GetAction(std::int32_t slot) {
            return HasAction(slot) ? m_actions[static_cast<std::size_t>(slot)] : nullptr;
        }

      private:
        SDK::wh::shared::C_Signal m_signal0008;                   // 0x08, ctor (0x1809CDAD0) dispatch unk_185665D58
        SDK::wh::shared::C_Signal m_signal0018;                   // 0x18, dispatch unk_185665D68
        SDK::CryStringT<char> m_name0028;                         // 0x28, ctor sets the empty string; the dtor (0x180FC1B6C) releases it
        std::uint16_t m_handle0030;                               // 0x30, ctor sets -1
        std::uint16_t m_handle0032;                               // 0x32, ctor sets -1
        std::byte m_padding0034[0x4];                             // 0x34
        SDK::StdVector<SDK::wh::framework::I_Action *> m_actions; // 0x38; intrusive pointers, borrowed only.
        SDK::StdVector<void *> m_vector0050;                      // 0x50, ctor zeroes, dtor frees
        std::int32_t m_slotCount0068;                             // 0x68, the ctor's int argument
        std::uint16_t m_state006C;                                // 0x6C, ctor sets 0
        std::byte m_padding006E[0x2];                             // 0x6E
        SDK::StdVector<void *> m_vector0070;                      // 0x70, ctor reserves 2, dtor frees
        std::uint64_t m_state0088;                                // 0x88, ctor sets 0

        static void AssertLayout() {
            static_assert(offsetof(C_ActionDirector, m_signal0008) == 0x08);
            static_assert(offsetof(C_ActionDirector, m_name0028) == 0x28);
            static_assert(offsetof(C_ActionDirector, m_vector0050) == 0x50);
            static_assert(offsetof(C_ActionDirector, m_slotCount0068) == 0x68);
            static_assert(offsetof(C_ActionDirector, m_vector0070) == 0x70);
            static_assert(offsetof(C_ActionDirector, m_actions) == 0x38);
        }
    };

    static_assert(sizeof(C_ActionDirector) == 0x90);
} // namespace SDK::wh::framework
