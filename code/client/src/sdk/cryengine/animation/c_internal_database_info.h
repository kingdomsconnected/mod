#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One loaded `.dba`: created fresh by every
     * synchronous load (`LoadDatabaseDBA`, 0x1837A11B4) and every stream
     * (`StartStreamingDBA`, 0x180956F9C), filled by
     * `CInternalDatabaseInfo::ReadController905` (0x180766210) and destroyed
     * by its destructor (0x180956E44), which frees the controller storage.
     * Two tables, the second at +0x08.
     */
    class CInternalDatabaseInfo {
      public:
        const char *GetFilePath() const {
            return m_strFilePath.m_pData;
        }

      private:
        // Members initialized by constructor 0x1809570F4.
        const void *m_pVTable;             // 0x00, IStreamCallback
        const void *m_pChainVTable;        // 0x08, IControllerRelocatableChain
        void *m_pNextChain;                // 0x10
        std::uint32_t m_FilePathCRC;       // 0x18
        std::uint32_t m_padding001C;
        CryStringT<char> m_strFilePath;    // 0x20
        bool m_bLoadFailed;                // 0x28
        std::byte m_padding0029[0x7];
        CryStringT<char> m_strLastError;   // 0x30
        std::int32_t m_iTotalControllers;  // 0x38
        std::uint32_t m_padding003C;
        void *m_hStorage;                  // 0x40, CControllerDefragHdl
        std::size_t m_nStorageLength;      // 0x48
        void *m_pControllersInplace;       // 0x50, CControllerOptNonVirtual

        static void AssertLayout() {
            static_assert(offsetof(CInternalDatabaseInfo, m_FilePathCRC) == 0x18, "CInternalDatabaseInfo::m_FilePathCRC offset mismatch");
            static_assert(offsetof(CInternalDatabaseInfo, m_strFilePath) == 0x20, "CInternalDatabaseInfo::m_strFilePath offset mismatch");
        }
    };

    static_assert(sizeof(CInternalDatabaseInfo) == 0x58, "CInternalDatabaseInfo size mismatch");
} // namespace SDK
