#pragma once

#include "../fmod/fmod_types.h"
#include "../patterns.h"
#include "../types.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh {
    /**
     * One bank the FMOD wrapper knows, `FSW_BANK` (0x78 bytes, constructed at 0x180B40540).
     *
     * Its events reach the wrapper's path map once `CWh_FmodStudioWrapper::Update` (0x180500F40)
     * sees the metadata loaded: `FetchEventDescriptions` reads every event's path -- which needs the
     * project's strings bank already loaded, and gives up for good on the first path it cannot read
     * -- then `LinkEventDescriptions` sets the state to 4, after which ATL triggers naming those
     * paths play.
     */
    class FSW_BANK {
      public:
        enum class EState : std::int32_t {
            None            = 0,
            MetadataLoading = 2,
            Linked          = 4,
        };

        /**
         * `LoadMetadataAsync` (0x180B40850): starts loading the bank (FMOD's non-blocking
         * loadBankFile), or only counts another holder when it already is. The game's own runtime
         * caller, `LoadBankMetadataByName` (0x182570834), passes (false, true) from the game thread.
         */
        void LoadMetadataAsync(bool isCinematic, bool loadSamples) {
            hook::this_call<void>(gPatterns.FSW_BANK__LoadMetadataAsync, this, isCinematic, loadSamples);
        }

        /**
         * `RequestMetadataUnload` (0x18256D980): stops every live instance of the bank's events and
         * asks Update to unload it. The bank stays registered; `LoadMetadataAsync` brings it back.
         */
        bool RequestMetadataUnload() {
            return hook::this_call<bool>(gPatterns.FSW_BANK__RequestMetadataUnload, this);
        }

        EState GetState() const {
            return m_state;
        }

        /** The FMOD bank, written by `loadBankFile` in LoadMetadataAsync (0x180B408DF); null until it starts. */
        SDK::Fmod::FMOD_STUDIO_BANK *GetBank() const {
            return m_pBank;
        }

      private:
        bool m_isCinematic;           // 0x00
        bool m_eventsFetched;         // 0x01
        bool m_unloadPending;         // 0x02
        bool m_loadSamples;           // 0x03
        EState m_state;               // 0x04
        std::int32_t m_state0008;     // 0x08, the ctor (0x180B40540) sets 1
        std::byte m_padding000C[0x4]; // 0x0C
        SDK::StdString m_name;        // 0x10, file name incl. .bank, relative to the data loader's root
        SDK::Fmod::FMOD_STUDIO_BANK *m_pBank; // 0x30
        SDK::StdVector<void *> m_eventDescriptions; // 0x38, FSW_EVENT_DESCRIPTION *
        SDK::StdVector<void *> m_snapshots;         // 0x50, FSW_SNAPSHOT *
        std::int32_t m_metadataRefCount;            // 0x68
        std::byte m_padding006C[0x4];               // 0x6C
        void *m_pLoadingTracker;                    // 0x70, owned

        static void AssertLayout() {
            static_assert(offsetof(FSW_BANK, m_loadSamples) == 0x03, "FSW_BANK::m_loadSamples offset mismatch");
            static_assert(offsetof(FSW_BANK, m_state) == 0x04, "FSW_BANK::m_state offset mismatch");
            static_assert(offsetof(FSW_BANK, m_name) == 0x10, "FSW_BANK::m_name offset mismatch");
            static_assert(offsetof(FSW_BANK, m_pBank) == 0x30, "FSW_BANK::m_pBank offset mismatch");
            static_assert(offsetof(FSW_BANK, m_metadataRefCount) == 0x68, "FSW_BANK::m_metadataRefCount offset mismatch");
            static_assert(offsetof(FSW_BANK, m_pLoadingTracker) == 0x70, "FSW_BANK::m_pLoadingTracker offset mismatch");
        }
    };

    static_assert(sizeof(FSW_BANK) == 0x78, "FSW_BANK size mismatch");
} // namespace SDK::wh
