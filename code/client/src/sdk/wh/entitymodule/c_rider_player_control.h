#pragma once

#include "../shared/c_signal.h"
#include "c_rider_player_input.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Player;

    /**
     * The local player's horse gameplay: which horse is theirs, the whistle
     * that calls it, and the mounted hints and soul effects.
     *
     * Owned by `C_Player` at +0xD68. The constructor (0x180A4D05C) installs
     * its own table 0x183A6CA50 at +0x00 and ISystemEventListener's
     * 0x183A6CA30 at +0x08, stores the player at +0x20, and its only
     * allocation site (0x180A4C5D0) proves the 0xA8 size. The primary table
     * has no base class behind it: two unnamed slots, a nullsub and the scalar
     * deleting destructor. The listener is held as its table pointer rather
     * than as a base, because a polymorphic first base would take +0x00.
     *
     * `SetPlayerHorse` (0x1817972A0) is what makes a horse the player's in
     * singleplayer terms, and it is destructive: it strips the new horse's
     * armour and moves the old horse's whole inventory across.
     */
    class C_RiderPlayerControl {
      public:
        virtual void unk_fn0000() = 0; // 0x1808EF440
        virtual void unk_fn0001() = 0; // 0x181A718F0
        virtual void unk_fn0002() = 0; // nullsub
        virtual void unk_fn0003() = 0; // scalar deleting destructor

        C_Player *GetPlayer() const {
            return m_pPlayer;
        }

        /**
         * The rider input this control owns, which `OnHorseRideStateChanged`
         * (0x180618018) binds to the ridden horse on ride states 1 and 2 and
         * unbinds otherwise (`C_RiderPlayerInput::SetHorse`).
         */
        C_RiderPlayerInput *GetRiderInput() const {
            return m_pRiderInput;
        }

      private:
        void *m_pSystemEventListenerVtable; // 0x008, ISystemEventListener
        wh::shared::C_Signal m_signal0010; // 0x010
        C_Player *m_pPlayer; // 0x020
        C_RiderPlayerInput *m_pRiderInput; // 0x028, owned
        std::uint16_t m_state0030; // 0x030, 1 by default
        bool m_state0032;          // 0x032
        std::byte m_padding0033[0x1];
        std::uint32_t m_state0034; // 0x034
        std::uint16_t m_state0038; // 0x038
        bool m_state003A;          // 0x03A
        std::byte m_padding003B[0x5];
        // A std::function<float()> bound to GetRiddenHorseActionState3Weight
        // (0x180A4F93C), MSVC's 0x40-byte layout.
        std::byte m_actionStateWeight[0x40]; // 0x040
        std::uint64_t m_state0080; // 0x080
        std::uint32_t m_state0088; // 0x088, copied from the horse cvars' +0xFC
        std::byte m_padding008C[0x4];
        std::uint16_t m_state0090; // 0x090
        std::byte m_padding0092[0x2];
        std::uint32_t m_state0094; // 0x094
        bool m_state0098;          // 0x098
        std::byte m_padding0099[0x7];
        std::uint64_t m_state00A0; // 0x0A0, seeded from a process-wide default

        static void AssertLayout() {
            static_assert(offsetof(C_RiderPlayerControl, m_pSystemEventListenerVtable) == 0x08, "C_RiderPlayerControl::m_pSystemEventListenerVtable offset mismatch");
            static_assert(offsetof(C_RiderPlayerControl, m_pPlayer) == 0x20, "C_RiderPlayerControl::m_pPlayer offset mismatch");
            static_assert(offsetof(C_RiderPlayerControl, m_pRiderInput) == 0x28, "C_RiderPlayerControl::m_pRiderInput offset mismatch");
        }
    };

    static_assert(sizeof(C_RiderPlayerControl) == 0xA8, "C_RiderPlayerControl size mismatch");
} // namespace SDK::wh::entitymodule
