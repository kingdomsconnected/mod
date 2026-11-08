#pragma once

#include "../framework/wuid.h"
#include "../shared/c_signal.h"
#include "i_soul_system.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * The RPG module's soul registry: every live soul, keyed by WUID, behind
     * the `I_SoulSystem` the global environment hands out (`C_RPGModule`
     * +0x80; constructor 0x180BEAE80, allocated 0x100220 bytes at
     * 0x180BE8F1D). Three vtables -- `I_SoulSystem` at +0x00, two more bases
     * at +0x08 and +0x10 -- before the soul storage.
     */
    class C_SoulList: public I_SoulSystem {
      public:
        /**
         * The WUID of the soul this machine's player is, `WUID::kInvalid`
         * (`qword_185332358`) until one is resolved.
         *
         * Written by `GetOrCreateSoul` for any entity whose actor answers
         * `C_Actor::IsPlayerControlled` -- the hit path at 0x18068A2C4 and the
         * new-soul path in `InitSoulFromEntity` at 0x180AD7FBA -- and reset by
         * the list's clear at 0x1817A0ECA. Read wherever the game means "the
         * player's soul": the HUD's health and stamina bars (0x18046ECDE),
         * POI discovery XP, sleep, fast travel, skill teachers and some forty
         * other sites.
         */
        framework::WUID GetPlayerSoulWuid() const {
            return m_playerSoulWuid;
        }

        void SetPlayerSoulWuid(framework::WUID wuid) {
            m_playerSoulWuid = wuid;
        }

      private:
        /**
         * One slot of the 65536-entry handle table at +0x60. Initialiser
         * 0x181EDF250 threads every slot onto a free list through the two
         * link words and nulls the object; what the first word counts and
         * which object type the slot holds are not proven.
         */
        struct S_HandleSlot {
            std::uint16_t m_word0000;  // 0x0; initialiser writes 0.
            std::uint16_t m_link0002;  // 0x2; neighbouring slot index.
            std::uint16_t m_link0004;  // 0x4; neighbouring slot index.
            std::byte m_padding0006[0x2];
            void *m_pObject;           // 0x8; initialiser writes null.
        };
        static_assert(sizeof(S_HandleSlot) == 0x10);

        std::byte m_vftable0008[0x8];              // 0x08; second C_SoulList vftable (ctor 0x180BEAE80).
        std::byte m_vftable0010[0x8];              // 0x10; third C_SoulList vftable.
        SDK::wh::shared::C_Signal m_signal0018;    // 0x18; dispatch unk_1856698F0.
        SDK::wh::shared::C_Signal m_signal0028;    // 0x28; dispatch unk_1856698B0.
        void *m_ptrs0038[5];                       // 0x38; ctor 0.
        // 0x60: handle table header (count 0xFFFF at +0, 0x20000 at +0x10) overlays the first slots; the initialiser writes both through the slot array.
        S_HandleSlot m_handleSlots[0x10000];       // 0x60
        void *m_ptrs100060[3];                     // 0x100060; ctor 0.
        std::uint32_t m_value100078;               // 0x100078; ctor 0.
        std::byte m_padding10007C[0x4];
        // 0x100080: an MSVC std::unordered_map (max load factor 1.0f, list 0x100088, buckets 0x100098, mask 7, maxidx 8); key and value types unproven.
        std::byte m_map100080[0x40];
        std::byte m_object1000C0[0x40];            // 0x1000C0; built by sub_1806E4638.
        std::uint32_t m_value100100;               // 0x100100; ctor 0.
        std::byte m_padding100104[0x4];
        framework::WUID m_playerSoulWuid; // 0x100108
        std::byte m_tree100110[0x10];              // 0x100110; std::map/set {head, size}; element type unproven.
        std::byte m_tree100120[0x10];              // 0x100120; a second std::map/set.
        std::byte m_object100130[0x40];            // 0x100130; built by sub_1806E4638, as at 0x1000C0.
        bool m_flag100170;                         // 0x100170; ctor 0.
        std::byte m_padding100171[0x7];
        std::byte m_object100178[0x80];            // 0x100178; built by sub_181023BEC.
        void *m_ptrs1001F8[3];                     // 0x1001F8; ctor 0.
        void *m_pList100210;                       // 0x100210; head node of a 0x38-byte-node std::list.
        void *m_ptr100218;                         // 0x100218; ctor 0.

        static void AssertLayout() {
            static_assert(offsetof(C_SoulList, m_playerSoulWuid) == 0x100108, "C_SoulList::m_playerSoulWuid offset mismatch");
            static_assert(offsetof(C_SoulList, m_signal0018) == 0x18, "C_SoulList::m_signal0018 offset mismatch");
            static_assert(offsetof(C_SoulList, m_handleSlots) == 0x60, "C_SoulList::m_handleSlots offset mismatch");
            static_assert(offsetof(C_SoulList, m_ptrs100060) == 0x100060, "C_SoulList::m_ptrs100060 offset mismatch");
            static_assert(offsetof(C_SoulList, m_map100080) == 0x100080, "C_SoulList::m_map100080 offset mismatch");
            static_assert(offsetof(C_SoulList, m_object100178) == 0x100178, "C_SoulList::m_object100178 offset mismatch");
            static_assert(offsetof(C_SoulList, m_pList100210) == 0x100210, "C_SoulList::m_pList100210 offset mismatch");
        }
    };

    static_assert(sizeof(C_SoulList) == 0x100220, "C_SoulList size mismatch");
} // namespace SDK::wh::rpgmodule
