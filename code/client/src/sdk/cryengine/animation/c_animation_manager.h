#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../../wh/shared/c_recursive_srw_lock.h"
#include "global_animation_header_aim.h"
#include "global_animation_header_caf.h"

#include <utils/hooking/hooking.h>

#include <cstddef>

namespace SDK {
    /**
     * CryAnimation's global clip registry, embedded in CharacterManager at +0x1C8.
     *
     * Every clip any skeleton can play has one header here, indexed by global id; an animation
     * set only holds ids into it. With `ca_UseIMG_CAF` on, the whole table is built from
     * `animations/animations.img` once per CharacterManager, and `CAnimationSet::LoadFileCAF`
     * then only looks paths up -- so a clip the image does not list needs its header created
     * here first.
     *
     * 0x1D8 bytes: the constructor's last write is at +0x1D0.
     */
    class CAnimationManager {
      public:
        /**
         * `CreateGAH_CAF` (0x180B14D68): the global id of the header for `path`, creating it when
         * no header has that path yet. The header starts empty; `LoadFileCAF` loads the clip into
         * it. Main thread, with every animation job synced and the manager's lock held, unless
         * the table is still being built.
         */
        int CreateGlobalHeaderCAF(const CryStringT<char> &path) {
            return hook::this_call<int>(gPatterns.CAnimationManager__CreateGAH_CAF, this, &path);
        }

        DynArray<GlobalAnimationHeaderCAF> &GetGlobalCAF() {
            return m_arrGlobalCAF;
        }

        DynArray<GlobalAnimationHeaderAIM> &GetGlobalAIM() {
            return m_arrGlobalAIM;
        }

        /**
         * Guards the global clip table against the streaming thread, which
         * creates headers for DBA contents the image does not list
         * (`ReadController905`, 0x180766210).
         */
        wh::shared::C_RecursiveSRWLock &GetLock() {
            return m_lock;
        }

      private:
        // CryAnimation's CAnimationManager order, from the constructor
        // 0x1808E9E90: the folder map, the DBA headers, the CAF name map, the
        // three global header arrays, then KCD2's locks and maps, the
        // shutting-down flag and the event listener it creates last.
        const void *m_pVTable;                              // 0x000
        std::byte m_AnimationsMap[0x40];                    // 0x008, an std::unordered_map (load factor 1, mask 7)
        DynArray<std::byte> m_arrGlobalHeaderDBA;           // 0x048, DynArray<CGlobalHeaderDBA>
        std::byte m_AnimationMapCAF[0x40];                  // 0x050, CNameCRCMap, built by 0x1806030E4
        std::byte m_SubFoldersMap[0x10];                    // 0x090, an std::map: head node and size
        void *m_vector00A0[3];                              // 0x0A0
        DynArray<GlobalAnimationHeaderCAF> m_arrGlobalCAF;  // 0x0B8
        DynArray<GlobalAnimationHeaderAIM> m_arrGlobalAIM;  // 0x0C0
        DynArray<std::byte> m_arrGlobalLMG;                 // 0x0C8, DynArray<GlobalAnimationHeaderLMG>
        void *m_lock00D0;                                   // 0x0D0, SRWLOCK
        std::uint64_t m_state00D8;                          // 0x0D8
        wh::shared::C_RecursiveSRWLock m_lock;              // 0x0E0
        std::uint32_t m_state00F0;                          // 0x0F0
        std::uint32_t m_padding00F4;
        void *m_lock00F8;                                   // 0x0F8, SRWLOCK
        std::uint64_t m_state0100;                          // 0x100
        std::byte m_map0108[0x10];                          // 0x108, an std::map
        std::byte m_map0118[0x10];                          // 0x118, an std::map
        std::byte m_map0128[0x10];                          // 0x128, an std::map
        std::uint64_t m_state0138;                          // 0x138
        std::uint32_t m_state0140;                          // 0x140
        std::uint32_t m_padding0144;
        std::uint64_t m_state0148[4];                       // 0x148
        std::uint32_t m_state0168;                          // 0x168
        std::uint32_t m_padding016C;
        bool m_shuttingDown;                                // 0x170
        std::byte m_padding0171[0x7];
        volatile std::int64_t m_state0178;                  // 0x178, cleared atomically
        std::byte m_animMemoryTracker[0x40];                // 0x180, SAnimMemoryTracker
        std::uint64_t m_state01C0;                          // 0x1C0, built by 0x1813EE4A8
        void *m_pEventListener;                             // 0x1C8, CAnimationManagerEventListener
        bool m_state01D0;                                   // 0x1D0, true by default
        std::byte m_padding01D1[0x7];

        static void AssertLayout() {
            static_assert(offsetof(CAnimationManager, m_arrGlobalHeaderDBA) == 0x48, "CAnimationManager::m_arrGlobalHeaderDBA offset mismatch");
            static_assert(offsetof(CAnimationManager, m_arrGlobalCAF) == 0xB8, "CAnimationManager::m_arrGlobalCAF offset mismatch");
            static_assert(offsetof(CAnimationManager, m_arrGlobalAIM) == 0xC0, "CAnimationManager::m_arrGlobalAIM offset mismatch");
            static_assert(offsetof(CAnimationManager, m_lock) == 0xE0, "CAnimationManager::m_lock offset mismatch");
            static_assert(offsetof(CAnimationManager, m_pEventListener) == 0x1C8, "CAnimationManager::m_pEventListener offset mismatch");
        }
    };

    static_assert(sizeof(CAnimationManager) == 0x1D8, "CAnimationManager size mismatch");
} // namespace SDK
