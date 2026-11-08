#pragma once

#include "../../types.h"
#include "i_dialog.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::dialogmodule {
    /**
     * One running conversation. Opaque except for where its `I_Dialog`
     * subobject sits.
     *
     * `+0x18` is what `C_DialogManager::GetPlayerDialog` (`0x18197CB94`)
     * hands out for the instance it holds. `sizeof` is the 1112-byte
     * `_Ref_count_obj2` that `0x180870D2C` allocates, less its 16-byte header.
     */
    class C_DialogInstance final {
      public:
        I_Dialog *GetDialog() {
            return reinterpret_cast<I_Dialog *>(&m_pDialogVtable);
        }

      private:
        // Typed from the ctor 0x1808DCC08 and dtor 0x1808D9FF8. Embedded objects are sized by the next written offset.
        void *m_vtables0000[3]; // 0x00, three of the six C_DialogInstance vptrs
        void *m_pDialogVtable;  // 0x18 I_Dialog subobject
        void *m_vtables0020[2]; // 0x20
        std::byte m_object0030[0x10];       // 0x30, built by 0x1808DCBEC
        std::uint64_t m_args0040[3];        // 0x40, three register arguments of the ctor
        std::byte m_object0058[0xE8];       // 0x58, built by 0x18093CBD4 from the fifth argument
        std::uint64_t m_state0140[3];       // 0x140, ctor zeroes
        void *m_pOwned0158;                 // 0x158, ctor zeroes; the dtor deletes it
        std::uint64_t m_state0160[3];       // 0x160, ctor zeroes; 0x168 and 0x170 have their own dtors
        std::byte m_list0178[0x10];         // 0x178, std::list
        std::byte m_object0188[0x38];       // 0x188, built by 0x1808DCA30
        std::byte m_object01C0[0x18];       // 0x1C0, built by 0x18079A6E8
        std::byte m_deque01D8[0x28];        // 0x1D8, std::deque
        std::byte m_deque0200[0x28];        // 0x200, std::deque
        std::uint64_t m_state0228[2];       // 0x228, ctor zeroes
        std::uint32_t m_state0238;          // 0x238, ctor sets 0x80000000
        std::byte m_unset023C[0x4];         // 0x23C, not written by the ctor
        SDK::StdVector<void *> m_vector0240; // 0x240, ctor zeroes
        std::uint64_t m_state0258;          // 0x258, ctor zeroes
        std::byte m_object0260[0x18];       // 0x260, built by 0x1808DCF30
        std::uint32_t m_state0278;          // 0x278, ctor sets 0
        std::byte m_unset027C[0x4];         // 0x27C, not written by the ctor
        std::uint64_t m_args0280[2];        // 0x280, the ctor's sixth and seventh arguments
        std::uint64_t m_state0290;          // 0x290, ctor zeroes
        std::uint32_t m_state0298;          // 0x298, ctor zeroes
        std::uint16_t m_state029C;          // 0x29C, ctor zeroes
        std::byte m_unset029E[0x2];         // 0x29E, not written by the ctor
        std::int32_t m_state02A0;           // 0x2A0, ctor sets -1
        std::uint32_t m_state02A4;          // 0x2A4, ctor zeroes
        std::uint64_t m_state02A8;          // 0x2A8, ctor sets qword_185322410
        std::uint8_t m_flag02B0;            // 0x2B0, ctor zeroes
        std::byte m_unset02B1[0x7];         // 0x2B1, not written by the ctor
        void *m_pOwned02B8[2];              // 0x2B8, two std::unique_ptr the dtor resets
        std::byte m_object02C8[0x20];       // 0x2C8, built by 0x1808DBE64 with the +0x10 subobject
        void *m_pOwned02E8;                 // 0x2E8, ctor zeroes; the dtor destroys it through vtable slot 0
        std::uint64_t m_state02F0;          // 0x2F0, ctor sets 19
        std::uint64_t m_state02F8;          // 0x2F8, ctor zeroes; the dtor destroys it with 0x1808DB160
        std::byte m_object0300[0x18];       // 0x300, built by 0x180416FA0
        std::byte m_object0318[0x8];        // 0x318, built by 0x1808DDC44
        std::uint8_t m_flag0320;            // 0x320, ctor zeroes
        std::byte m_unset0321[0x7];         // 0x321, not written by the ctor
        std::byte m_object0328[0x18];       // 0x328
        SDK::StdVector<void *> m_vector0340; // 0x340, ctor zeroes, dtor frees
        std::byte m_unset0358[0x8];         // 0x358, not written by the ctor
        std::uint64_t m_state0360[2];       // 0x360, ctor zeroes
        std::byte m_unset0370[0x10];        // 0x370, not written by the ctor
        std::uint32_t m_state0380;          // 0x380, ctor zeroes
        std::byte m_unset0384[0x4];         // 0x384, not written by the ctor
        std::uint64_t m_state0388[3];       // 0x388, ctor zeroes; the dtor destroys it with 0x1823CA91C
        std::uint32_t m_state03A0;          // 0x3A0, ctor zeroes
        std::uint16_t m_state03A4;          // 0x3A4, ctor sets 0x100
        std::byte m_unset03A6[0x2];         // 0x3A6, not written by the ctor
        std::uint64_t m_state03A8;          // 0x3A8, ctor zeroes
        std::byte m_intermission03B0[0x20]; // 0x3B0, built by 0x1808DD5C0 for "DialogIntermission"
        std::byte m_state03D0[0x10];        // 0x3D0, ctor loads xmmword_18409A240
        std::uint64_t m_state03E0;          // 0x3E0, ctor zeroes
        std::uint32_t m_state03E8;          // 0x3E8, ctor zeroes
        std::uint8_t m_flag03EC;            // 0x3EC, ctor zeroes
        std::byte m_unset03ED[0x3];         // 0x3ED, not written by the ctor
        std::uint64_t m_state03F0;          // 0x3F0, ctor zeroes
        std::byte m_set03F8[0x10];          // 0x3F8, std::set
        std::uint32_t m_state0408;          // 0x408, ctor zeroes
        std::uint16_t m_state040C;          // 0x40C, ctor zeroes
        std::byte m_unset040E[0x2];         // 0x40E, not written by the ctor
        std::uint64_t m_state0410;          // 0x410, ctor sets 9
        std::uint8_t m_flag0418;            // 0x418, ctor zeroes
        std::byte m_unset0419[0x3];         // 0x419, not written by the ctor
        std::int32_t m_state041C;           // 0x41C, ctor sets -1
        SDK::StdVector<void *> m_vector0420; // 0x420, ctor zeroes, dtor frees
        std::uint16_t m_state0438;          // 0x438, ctor zeroes
        std::uint8_t m_flag043A;            // 0x43A, ctor zeroes
        std::byte m_unset043B[0x1];         // 0x43B, not written by the ctor
        std::uint32_t m_state043C;          // 0x43C, ctor zeroes
        std::uint8_t m_flag0440;            // 0x440, ctor zeroes
        std::byte m_unset0441[0x7];         // 0x441, not written by the ctor

        static void AssertLayout() {
            static_assert(offsetof(C_DialogInstance, m_pDialogVtable) == 0x18, "C_DialogInstance::m_pDialogVtable offset mismatch");
            static_assert(offsetof(C_DialogInstance, m_object0058) == 0x58, "C_DialogInstance::m_object0058 offset mismatch");
            static_assert(offsetof(C_DialogInstance, m_vector0240) == 0x240, "C_DialogInstance::m_vector0240 offset mismatch");
            static_assert(offsetof(C_DialogInstance, m_pOwned02E8) == 0x2E8, "C_DialogInstance::m_pOwned02E8 offset mismatch");
            static_assert(offsetof(C_DialogInstance, m_intermission03B0) == 0x3B0, "C_DialogInstance::m_intermission03B0 offset mismatch");
            static_assert(offsetof(C_DialogInstance, m_vector0420) == 0x420, "C_DialogInstance::m_vector0420 offset mismatch");
        }
    };

    static_assert(sizeof(C_DialogInstance) == 0x448, "C_DialogInstance size mismatch");
} // namespace SDK::wh::dialogmodule
