#pragma once

#include "../../shared/e_dlc.h"

#include <cstdint>

namespace SDK::wh::game::dlc {
    /**
     * The DLC service at `C_GlobalEnvironment + 0x80`, built during game boot.
     *
     * `IsInstalled` resolves through `C_DLCPackage_PC::IsInstalled`
     * (0x1839C344C): app id 0 is never owned, the sentinel 219957777 always
     * is, and anything else asks `Cry::GamePlatform`, whose Steam service
     * builds its licence list from `ISteamApps::BIsDlcInstalled` -- so this
     * reports owned *and* downloaded.
     *
     * The table at 0x18408FA40 holds 19 slots. There is deliberately no C++
     * virtual destructor: the native one is slot 15, in the middle of the
     * table, and slot 8 returns a `CryStringT<char>` by value, so both are
     * left opaque.
     */
    class I_DLCManager {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual bool IsInstalled(shared::E_DLC::Type dlc) = 0; // +0x10
        virtual bool TryGetForcedState(shared::E_DLC::Type dlc, bool *state) = 0; // +0x18
        virtual std::uint8_t GetDLCState(shared::E_DLC::Type dlc) = 0; // +0x20
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void NotifyAllListeners() = 0; // +0x38
        virtual void unk_fn0008() = 0;
        virtual std::uint32_t GetUIOrder(shared::E_DLC::Type dlc) = 0; // +0x48
        virtual bool AffectsSavegame(shared::E_DLC::Type dlc) = 0; // +0x50
        virtual void unk_fn0011() = 0;
        virtual bool IsFreeDLC(shared::E_DLC::Type dlc) = 0; // +0x60
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void Init() = 0; // +0x80
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
    };

    static_assert(sizeof(I_DLCManager) == 0x8, "I_DLCManager size mismatch");
} // namespace SDK::wh::game::dlc
