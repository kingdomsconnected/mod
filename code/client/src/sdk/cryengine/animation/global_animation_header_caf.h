#pragma once

#include "i_controller.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * The global, skeleton-independent header of one `.caf` clip, shared by
     * every skeleton whose animation list names it.
     *
     * One array of them, 0xA8 apart, at `CharacterManager::m_arrGlobalCAF`:
     * `CAnimationManager::CreateGAH_CAF` (0x180B14D68) appends them and
     * `CInternalDatabaseInfo::ReadController905` (0x180766210) indexes them
     * at that stride.
     *
     * Unlike CryEngine's it is polymorphic (table 0x183A7D9B8), and it keeps
     * no path of its own: slot 1 looks the path up through a shared table
     * that can insert, so it is not called from here. `CreateGAH_CAF` stores
     * the path's case-insensitive 64-bit hash at +0x10 instead.
     *
     * Its tracks are keyed by the CRC32 of the exact joint name, as the
     * skeleton records it (`CDefaultSkeleton` joint `m_nJointCRC32`, read
     * from the `.chr`), and found by `GetControllerByJointCRC32`
     * (0x180844184): a binary search over the sorted ids at +0x40 answering
     * the controller at the same index of +0x38, or null when the asset is
     * not loaded.
     */
    class GlobalAnimationHeaderCAF {
      public:
        /** `CA_ASSET_ADDITIVE`: the clip's tracks are deltas over the bind pose. */
        static constexpr std::uint32_t kFlagAdditive = 0x1;

        virtual void unk_fn0000() = 0; // +0x00
        virtual void unk_fn0001() = 0; // +0x08

        bool IsAdditive() const {
            return (m_nFlags & kFlagAdditive) != 0;
        }

        std::uint64_t GetFilePathHash() const {
            return m_nFilePathHash;
        }

        /** The clip's controllers, one per animated joint, sorted by joint CRC once it is loaded. */
        IController **GetControllers() const {
            return m_arrController;
        }

      private:
        std::uint64_t m_state0008;        // 0x08, not written by the constructor at 0x180B7C3F4
        std::uint64_t m_nFilePathHash;    // 0x10, CityHash64 of the lower-cased path (0x1806A3014)
        std::uint32_t m_nFlags;           // 0x18, the CA_ASSET_* bits both samplers test
        std::uint32_t m_state001C;        // 0x1C, not written by the constructor
        void *m_pointer0020;              // 0x20, not written by the constructor
        std::uint32_t m_FilePathDBACRC32; // 0x28, -1 in the constructor
        float m_fStartSec;                // 0x2C, -1.0
        float m_fEndSec;                  // 0x30, -1.0
        float m_fTotalDuration;           // 0x34, -1.0
        IController **m_arrController;    // 0x38, DynArray data; the count sits 4 bytes before it
        std::uint32_t *m_arrControllerLookupVector; // 0x40, DynArray<uint32> data: sorted joint CRCs
        std::byte m_AnimEventsCAF[0x18];  // 0x48, CAnimEventList: vtable, event-array pointer, zeroed qword
        std::uint32_t m_state0060;        // 0x60, zero
        float m_state0064[7];             // 0x64, all 1.0 in the constructor
        QuatT m_StartLocation;            // 0x80, identity
        std::uint32_t m_state009C;        // 0x9C, zero
        std::int32_t m_state00A0;         // 0xA0, path index; -1 in the constructor, released by the destructor when negative
        std::uint8_t m_state00A4;         // 0xA4, 1 in the constructor
        std::byte m_pad00A5[0x3];         // 0xA5

        static void AssertLayout() {
            static_assert(offsetof(GlobalAnimationHeaderCAF, m_nFilePathHash) == 0x10, "GlobalAnimationHeaderCAF::m_nFilePathHash offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderCAF, m_nFlags) == 0x18, "GlobalAnimationHeaderCAF::m_nFlags offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderCAF, m_FilePathDBACRC32) == 0x28, "GlobalAnimationHeaderCAF::m_FilePathDBACRC32 offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderCAF, m_fTotalDuration) == 0x34, "GlobalAnimationHeaderCAF::m_fTotalDuration offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderCAF, m_AnimEventsCAF) == 0x48, "GlobalAnimationHeaderCAF::m_AnimEventsCAF offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderCAF, m_StartLocation) == 0x80, "GlobalAnimationHeaderCAF::m_StartLocation offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderCAF, m_state00A0) == 0xA0, "GlobalAnimationHeaderCAF::m_state00A0 offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderCAF, m_arrController) == 0x38, "GlobalAnimationHeaderCAF::m_arrController offset mismatch");
        }
    };

    static_assert(sizeof(GlobalAnimationHeaderCAF) == 0xA8, "GlobalAnimationHeaderCAF size mismatch");

    /**
     * Joint-name CRC32s of the two human skeletons' roots, where a clip keeps
     * its root motion. `male.chr` roots at `Human_Male`, `female.chr` at
     * `Human_Female`, which is why a male clip on the female skeleton has no
     * root track to find. Both values are the plain CRC32 of the name and
     * were read out of the `.chr` files, not computed from a guess.
     */
    inline constexpr std::uint32_t kHumanMaleRootJointCrc   = 0xEEBAFD38;
    inline constexpr std::uint32_t kHumanFemaleRootJointCrc = 0x4128E6D9;
} // namespace SDK
