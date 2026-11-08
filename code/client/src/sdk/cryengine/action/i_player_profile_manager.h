#pragma once

#include "i_player_profile.h"
#include "i_player_profile_listener.h"

namespace SDK {
    /**
     * `CPlayerProfileManager`, owned by CCryAction at +0x590 and returned by
     * `IGameFramework::GetIPlayerProfileManager` (+0x128). The game's
     * `PlayerProfileWHManager` logs the user in and loads
     * `%USER%/Profiles/settings.xml` through it.
     *
     * Slots are the vtable at 0x184059898. Stock CryEngine order up to
     * GetDefaultProfile; KCD2 has one extra entry after it (+0xD8), which is
     * why AddListener sits one slot later than upstream. Unproven entries
     * keep their position under `Opaque` names.
     *
     * Game thread only.
     */
    struct IPlayerProfileManager {
        /** What SaveProfile reports through its out parameter; 0 is success. */
        enum EProfileOperationResult : int {
            ePOR_Success = 0,
        };

        /** CryEngine's `EProfileReasons`: what a save or load is for. */
        enum EProfileReasons : unsigned int {
            ePR_Game    = 0x01,
            ePR_Options = 0x02,
            ePR_All     = 0xFF,
        };

        virtual ~IPlayerProfileManager() = default; // +0x000
        virtual void Opaque01()          = 0;       // Initialize
        virtual void Opaque02()          = 0;       // Shutdown
        virtual void Opaque03()          = 0;       // GetMemoryStatistics
        virtual void Opaque04()          = 0;       // GetUserCount
        virtual void Opaque05()          = 0;       // GetUserInfo
        virtual void Opaque06()          = 0;       // LoginUser
        virtual void Opaque07()          = 0;       // LogoutUser
        virtual void Opaque08()          = 0;       // GetProfileCount
        virtual void Opaque09()          = 0;       // GetProfileInfo
        virtual void Opaque10()          = 0;       // SetProfileLastLoginTime
        virtual void Opaque11()          = 0;       // CreateProfile
        virtual void Opaque12()          = 0;       // DeleteProfile
        virtual void Opaque13()          = 0;       // RenameProfile

        /**
         * Broadcasts SaveToProfile to every listener, then writes the profile.
         * `CPlayerProfileManager::SaveProfile` (0x180602868).
         */
        virtual bool SaveProfile(const char *userId, EProfileOperationResult &result, unsigned int reason) = 0; // +0x070

        virtual void Opaque15() = 0; // SaveInactiveProfile
        virtual void Opaque16() = 0; // IsLoadingProfile
        virtual void Opaque17() = 0; // IsSavingProfile
        virtual void Opaque18() = 0; // ActivateProfile

        /** 0x180602368: the user's entry, or null before login. */
        virtual IPlayerProfile *GetCurrentProfile(const char *userId) = 0; // +0x098

        /** A folded getter at 0x180602360 returning the id held at +0x38. */
        virtual const char *GetCurrentUser() = 0; // +0x0A0

        virtual void Opaque21() = 0; // GetCurrentUserIndex
        virtual void Opaque22() = 0; // SetExclusiveControllerDeviceIndex
        virtual void Opaque23() = 0; // GetExclusiveControllerDeviceIndex
        virtual void Opaque24() = 0; // ResetProfile
        virtual void Opaque25() = 0; // ReloadProfile
        virtual void Opaque26() = 0;
        virtual void Opaque27() = 0;
        virtual void Opaque28() = 0; // PreviewProfile
        virtual void Opaque29() = 0; // SetSharedSaveGameFolder
        virtual void Opaque30() = 0;

        /**
         * `CPlayerProfileManager::AddListener` (0x180602298). With `updateNow`
         * the listener is sent LoadFromProfile for the current profile at
         * once, so a late registrant still starts from the saved values.
         */
        virtual void AddListener(IPlayerProfileListener *listener, bool updateNow) = 0; // +0x0F8

        /** A folded vector erase at 0x1836CBBC8, shared with CGameTokenSystem. */
        virtual void RemoveListener(IPlayerProfileListener *listener) = 0; // +0x100
    };
} // namespace SDK
