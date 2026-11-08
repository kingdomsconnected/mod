#pragma once

#include "../patterns.h"
#include "../types.h"
#include "fsw_bank.h"

#include <utils/hooking/hooking.h>

#include "../fmod/fmod_types.h"
#include "../patterns.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh {
    /**
     * The game's grip on FMOD: Warhorse's fork of CryEngine's
     * `CryAudio::Impl::Fmod::CImpl`, built from `CryAudioImplFmod`.
     *
     * There is exactly one, reached through `GetInstance`. It owns the Studio
     * system and its core system, which is why the mod does not open an audio
     * device of its own: voice played on this core system comes out of the
     * player's chosen output, under the game's mixer, positioned against the
     * game's listener.
     *
     * The binary spells the class three ways -- the vtable symbol says
     * `CWh_FmodStudioWrapper`, the log strings `CB_FmodStudioWrapper`, the
     * allocation tag `C_FmodStudioWrapper`. The vtable symbol is a surviving
     * compiler symbol, so that is the name used here.
     */
    class CWh_FmodStudioWrapper {
      public:
        /** How far `InitializeFmodStudio` has got. Only Ready may be used. */
        enum class EState : std::int32_t {
            Uninitialized = 0,
            Initializing  = 1,
            Ready         = 2,
        };

        static CWh_FmodStudioWrapper *GetInstance() {
            return *reinterpret_cast<CWh_FmodStudioWrapper **>(gPatterns.CWh_FmodStudioWrapper__Instance);
        }

        /**
         * The audio system is up and both system pointers are live.
         *
         * A runtime condition rather than a fact: the wrapper is constructed
         * long before `InitializeFmodStudio` writes this field, which it does
         * last, and an injected client runs before either.
         */
        bool IsReady() const {
            return m_state == EState::Ready;
        }

        SDK::Fmod::FMOD_STUDIO_SYSTEM *GetStudioSystem() const {
            return m_pStudioSystem;
        }

        SDK::Fmod::FMOD_SYSTEM *GetCoreSystem() const {
            return m_pCoreSystem;
        }

        /**
         * The listener the game last pushed to FMOD, in FMOD's own space.
         *
         * Cached by `SetListenerAttributes` from the same struct it hands to
         * `Studio::System::setListenerAttributes`, so it is the exact pose
         * everything else in the game is mixed against -- velocity included,
         * which the game zeroes to disable doppler.
         */
        /**
         * `RegisterBank` (0x180B403E8): adds a bank by file name -- `foo.bank`, relative to
         * `<game folder>/sounds/fmod/Build/PC/`, read through CryPak -- and returns its wrapper, or
         * null when the name is already registered or the wrapper is not running. Init registers
         * every bank of that folder once; this is the same call for one that arrived later. The
         * wrapper keeps the bank for the life of the process, so a name registers once.
         */
        FSW_BANK *RegisterBank(const char *fileName) {
            SDK::StdString name = SDK::StdString::HandedOver(fileName);
            return hook::this_call<FSW_BANK *>(gPatterns.CWh_FmodStudioWrapper__RegisterBank, this, &name);
        }

        const SDK::Fmod::FMOD_3D_ATTRIBUTES &GetListenerAttributes() const {
            return m_listenerAttributes;
        }

        /**
         * Whether the game has muffled its audio for losing window focus.
         *
         * `CWh_AudioImplFmod::OnLoseFocus` (0x1803B4BD0, IAudioImpl slot 7)
         * starts the Studio snapshot `snapshot:/setups/setup_lose_focus` and
         * keeps its instance here; `OnGetFocus` (0x1803B4AFC) stops it and
         * clears the field. Neither runs while `wh_snd_ignore_focus` is 1.
         * The banks define the snapshot's effect on each bus. Treat
         * everything under the Studio master bus as silenced while it is set.
         */
        bool IsLoseFocusSnapshotActive() const {
            return m_pLoseFocusSnapshot != nullptr;
        }

      private:
        void *m_pVTable;                                    // 0x000
        SDK::Fmod::FMOD_3D_ATTRIBUTES m_listenerAttributes; // 0x008
        // 0x038..0x2DF is the CB_FmodStudioWrapper base, built by 0x180E3C9E4; 0x2E0.. is this class's (ctor 0x18192F808).
        std::uint32_t m_state0038;                          // 0x038, base ctor zeroes
        std::byte m_unset003C[0x1C];                        // 0x03C, not written by either ctor
        std::uint64_t m_state0058;                          // 0x058, base ctor zeroes
        std::uint32_t m_state0060;                          // 0x060, base ctor zeroes
        std::byte m_unset0064[0xC];                         // 0x064, not written by either ctor
        std::byte m_object0070[0x30];                       // 0x070, built by 0x18091865C
        void *m_lock00A0;                                   // 0x0A0, SRWLOCK of the recursive lock
        std::uint64_t m_lockOwner00A8;                      // 0x0A8, base ctor zeroes
        void *m_lock00B0;                                   // 0x0B0, SRWLOCK
        std::uint64_t m_state00B8[4];                       // 0x0B8, base ctor zeroes
        EState m_state;                                     // 0x0D8
        std::int32_t m_lastInitError;                       // 0x0DC
        SDK::Fmod::FMOD_STUDIO_SYSTEM *m_pStudioSystem;     // 0x0E0
        SDK::Fmod::FMOD_SYSTEM *m_pCoreSystem;              // 0x0E8
        void *m_pBankWrapper;                               // 0x0F0, FSW_BANK
        SDK::Fmod::FMOD_STUDIO_BANK *m_pStringsBank;        // 0x0F8, loaded raw and synchronously by InitializeFmodStudio (0x180D2CDEE)
        std::uint64_t m_state0100[12];                      // 0x100, base ctor zeroes
        bool m_flag0160;                                    // 0x160, base ctor clears
        std::byte m_unset0161[0x7];                         // 0x161, not written by either ctor
        std::uint64_t m_state0168[9];                       // 0x168, base ctor zeroes
        void *m_pMapHead01B0;                               // 0x1B0, std::map head node (0x40-byte nodes)
        std::size_t m_mapSize01B8;                          // 0x1B8
        std::byte m_list01C0[0x10];                         // 0x1C0, std::list
        void *m_lock01D0;                                   // 0x1D0, SRWLOCK
        std::uint64_t m_state01D8[4];                       // 0x1D8, base ctor zeroes
        std::uint32_t m_state01F8;                          // 0x1F8, base ctor zeroes
        std::byte m_unset01FC[0xC];                         // 0x1FC, not written by either ctor
        std::byte m_object0208[0x24];                       // 0x208, built by 0x1803AAA30
        std::uint32_t m_state022C;                          // 0x22C, base ctor zeroes
        bool m_flag0230;                                    // 0x230, base ctor clears
        std::byte m_unset0231[0x7];                         // 0x231, not written by either ctor
        SDK::Fmod::FMOD_STUDIO_EVENTINSTANCE *m_pLoseFocusSnapshot; // 0x238
        std::uint64_t m_state0240;                                  // 0x240, base ctor zeroes; short-silence.ogg first
        SDK::StdVector<void *> m_vector0248;                        // 0x248, base ctor zeroes
        SDK::StdVector<void *> m_vector0260;                        // 0x260, 18-byte elements the base ctor clears
        SDK::StdVector<void *> m_vector0278;                        // 0x278, 8-byte elements the base ctor clears
        void *m_pMapHead0290;                                       // 0x290, std::map head node
        std::size_t m_mapSize0298;                                  // 0x298
        void *m_pMapHead02A0;                                       // 0x2A0, std::map head node (0x48-byte nodes)
        std::size_t m_mapSize02A8;                                  // 0x2A8
        void *m_pMapHead02B0;                                       // 0x2B0, std::map head node (0x58-byte nodes)
        std::size_t m_mapSize02B8;                                  // 0x2B8
        void *m_pMapHead02C0;                                       // 0x2C0, std::map head node (0x48-byte nodes)
        std::size_t m_mapSize02C8;                                  // 0x2C8
        void *m_lock02D0;                                           // 0x2D0, SRWLOCK
        std::uint64_t m_state02D8;                                  // 0x2D8, base ctor zeroes
        std::uint64_t m_state02E0[2];                               // 0x2E0, ctor zeroes
        float m_state02F0;                                          // 0x2F0, ctor sets 0.5
        bool m_flag02F4;                                            // 0x2F4, ctor clears
        std::byte m_unset02F5[0x3];                                 // 0x2F5, not written by the ctor
        void *m_pSetHead02F8;                                       // 0x2F8, std::set<int> head node
        std::size_t m_setSize0300;                                  // 0x300
        void *m_pSetHead0308;                                       // 0x308, std::set<int> head node
        std::size_t m_setSize0310;                                  // 0x310
        void *m_lock0318;                                           // 0x318, SRWLOCK
        std::uint64_t m_state0320[4];                               // 0x320, ctor zeroes
        bool m_flag0340;                                            // 0x340, ctor clears
        std::byte m_unset0341[0x7];                                 // 0x341, not written by the ctor
        void *m_pMapHead0348;                                       // 0x348, std::map head node (0x48-byte nodes)
        std::size_t m_mapSize0350;                                  // 0x350

        static void AssertLayout() {
            static_assert(offsetof(CWh_FmodStudioWrapper, m_listenerAttributes) == 0x008, "CWh_FmodStudioWrapper::m_listenerAttributes offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_state) == 0x0D8, "CWh_FmodStudioWrapper::m_state offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_lastInitError) == 0x0DC, "CWh_FmodStudioWrapper::m_lastInitError offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_pStudioSystem) == 0x0E0, "CWh_FmodStudioWrapper::m_pStudioSystem offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_pCoreSystem) == 0x0E8, "CWh_FmodStudioWrapper::m_pCoreSystem offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_pBankWrapper) == 0x0F0, "CWh_FmodStudioWrapper::m_pBankWrapper offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_pStringsBank) == 0x0F8, "CWh_FmodStudioWrapper::m_pStringsBank offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_lock00A0) == 0x0A0, "CWh_FmodStudioWrapper::m_lock00A0 offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_flag0160) == 0x160, "CWh_FmodStudioWrapper::m_flag0160 offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_object0208) == 0x208, "CWh_FmodStudioWrapper::m_object0208 offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_pLoseFocusSnapshot) == 0x238, "CWh_FmodStudioWrapper::m_pLoseFocusSnapshot offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_vector0248) == 0x248, "CWh_FmodStudioWrapper::m_vector0248 offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_state02E0) == 0x2E0, "CWh_FmodStudioWrapper::m_state02E0 offset mismatch");
            static_assert(offsetof(CWh_FmodStudioWrapper, m_pMapHead0348) == 0x348, "CWh_FmodStudioWrapper::m_pMapHead0348 offset mismatch");
        }
    };

    // From the tagged allocation that produces it, at 0x180B40225:
    // `mov edx, 358h` against the tag "C_FmodStudioWrapper".
    static_assert(sizeof(CWh_FmodStudioWrapper) == 0x358, "CWh_FmodStudioWrapper size mismatch");
} // namespace SDK::wh
