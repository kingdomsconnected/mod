#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "i_animation_set.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation animation-set implementation.
     *
     * RTTI proves the global hierarchy CAnimationSet -> IAnimationSet. Its
     * table at 0x18406BB38 has exactly the 40 inherited entries and adds no
     * primary virtual operations. Two creation paths and sized deletion
     * independently prove the complete 0xB0-byte layout.
     */
    class CAnimationSet : public IAnimationSet {
      public:
        ~CAnimationSet() override = default;

        /**
         * `LoadFileCAF` (0x18083F874): appends the clip at `path` to this set under `name` and
         * loads it synchronously the first time any set takes it. Both keys are
         * `CityHash64Lowercase`. Returns the clip's index in the set, or -1 when the name is
         * already taken.
         *
         * In image mode it only looks the path up in the global table and does not check the
         * answer, so the header must exist first (`CAnimationManager::CreateGlobalHeaderCAF`):
         * a path the table lacks appends global id -1 and touches the entry before the table.
         * Main thread, with every animation job synced -- the set's clip array reallocates.
         */
        int LoadFileCAF(std::uint64_t pathHash, std::uint64_t nameHash, const char *path, const char *name) {
            return hook::this_call<int>(gPatterns.CAnimationSet__LoadFileCAF, this, pathHash, nameHash, path, name);
        }

      private:
        // Writes of the constructor at 0x180AFA470 (CAnimationSet(const char *)).
        void *m_pointer0008;               // 0x08, 0x185675F08 in the constructor
        void *m_pointer0010;               // 0x10, null
        void *m_pointer0018;               // 0x18, null
        void *m_pointer0020;               // 0x20, null
        std::byte m_hashMap0028[0x40];     // 0x28, MSVC std::unordered_map: max load 1.0, list head, bucket vector, mask 7, max index 8
        std::byte m_map0068[0x10];         // 0x68, MSVC std::map: head node, size
        std::uint32_t m_state0078;         // 0x78, zero
        std::byte m_pad007C[0x4];          // 0x7C
        CryStringT<char> m_strFilePath;    // 0x80, the constructor's path argument
        void *m_pointer0088[4];            // 0x88, nulled by 0x180AFA5F8, which then reserves one entry
        std::uint16_t m_state00A8;         // 0xA8, zero
        std::byte m_pad00AA[0x6];          // 0xAA

        static void AssertLayout() {
            static_assert(offsetof(CAnimationSet, m_hashMap0028) == 0x28, "CAnimationSet::m_hashMap0028 offset mismatch");
            static_assert(offsetof(CAnimationSet, m_map0068) == 0x68, "CAnimationSet::m_map0068 offset mismatch");
            static_assert(offsetof(CAnimationSet, m_strFilePath) == 0x80, "CAnimationSet::m_strFilePath offset mismatch");
            static_assert(offsetof(CAnimationSet, m_state00A8) == 0xA8, "CAnimationSet::m_state00A8 offset mismatch");
        }
    };

    static_assert(sizeof(CAnimationSet) == 0xB0, "CAnimationSet size mismatch");
} // namespace SDK
