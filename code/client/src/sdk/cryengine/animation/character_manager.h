#pragma once

#include "../../types.h"
#include "../../wh/shared/c_recursive_srw_lock.h"
#include "../system/i_system_event_listener.h"
#include "c_animation_manager.h"
#include "global_animation_header_aim.h"
#include "global_animation_header_caf.h"
#include "i_character_manager.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation character manager.
     *
     * RTTI proves ICharacterManager at +0x00 and ISystemEventListener at
     * +0x08. The primary table at 0x184061730 implements all 64 manager
     * entries and appends two concrete operations; the secondary table at
     * 0x184061948 contains exactly the three listener entries. Constructor
     * and sized-deletion evidence prove the complete 0x805A8-byte layout.
     *
     * Its CAnimationManager is embedded at +0x1C8; the two global clip
     * arrays are that manager's +0xB8 and +0xC0, which
     * `CAnimationManager::CreateGAH_CAF` (0x180B14D68) and `CreateGAH_AIM`
     * (0x180B14848) append to. Published as `g_pCharacterManager`
     * (0x185189BE0) and gEnv+0xD0.
     */
    class CharacterManager : public ICharacterManager,
                             public ISystemEventListener {
      public:
        ~CharacterManager() override = default;

        virtual void unk_fn0064() = 0;
        virtual void unk_fn0065() = 0; // +0x208

        DynArray<GlobalAnimationHeaderCAF> &GetGlobalCAF() {
            return m_animationManager.GetGlobalCAF();
        }

        DynArray<GlobalAnimationHeaderAIM> &GetGlobalAIM() {
            return m_animationManager.GetGlobalAIM();
        }

        CAnimationManager &GetAnimationManager() {
            return m_animationManager;
        }

        /**
         * Whether `LoadAnimationImageFiles` (0x18091C948) has run for this manager. It sets the
         * flag on entry and builds the global clip table from the animation image, refusing to
         * when the table already holds a header -- so nothing may create one before this is set.
         */
        bool HasLoadedAnimationImages() const {
            return m_animationImagesLoaded != 0;
        }

        /**
         * Guards the global clip table against the streaming thread, which creates headers for
         * DBA contents the image does not list (`ReadController905`, 0x180766210).
         */
        wh::shared::C_RecursiveSRWLock &GetAnimationLock() {
            return m_animationManager.GetLock();
        }

      private:
        // CryAnimation's CharacterManager order, from the constructor
        // 0x18099BF04: the four per-frame vectors, the update counter and
        // start gate, the model caches, the animation manager, the facial
        // animation it allocates, the param loader, the 200 frame times, the
        // CDF cache, the context sync queue and KCD2's large tail.
        void *m_arrSkeletonUpdates[3];           // 0x010, std::vector<string>
        void *m_arrAnimPlaying[3];               // 0x028, std::vector<uint32>
        void *m_arrForceSkeletonUpdates[3];      // 0x040, std::vector<uint32>
        void *m_arrVisible[3];                   // 0x058, std::vector<uint32>
        std::uint32_t m_nUpdateCounter;          // 0x070
        std::uint32_t m_AllowStartOfAnimation;   // 0x074, 1 by default
        void *m_pStreamingListener;              // 0x078, IAnimationStreamingListener
        std::uint64_t m_state0080;               // 0x080
        std::uint32_t m_StartGAH_Iterator;       // 0x088
        std::int32_t m_animationImagesLoaded;    // 0x08C, m_IMGLoadedFlags; LoadAnimationImageFiles' once-per-manager guard
        void *m_arrModelCacheSKEL[3];            // 0x090, std::vector<CDefaultSkeletonReferences>
        std::byte m_state00A8[0x50];             // 0x0A8, built by 0x180ABB430
        std::byte m_state00F8[0x40];             // 0x0F8, built by 0x18099C144
        std::byte m_state0138[0x40];             // 0x138, built by 0x18099C168
        std::byte m_state0178[0x50];             // 0x178, built by 0x180ABB430
        CAnimationManager m_animationManager;    // 0x1C8
        void *m_pFacialAnimation;                // 0x3A0, CFacialAnimation, a 0x40-byte object built by 0x18099B8A0
        std::byte m_ParamLoader[0x18];           // 0x3A8, CChrParamLoader
        float *m_arrFrameTimes;                  // 0x3C0, std::vector<f32> resized to 200 entries
        float *m_arrFrameTimesEnd;               // 0x3C8
        float *m_arrFrameTimesCapacity;          // 0x3D0
        void *m_arrCacheForCDF[3];               // 0x3D8, std::vector<CharacterDefinition>
        std::byte m_ContextSyncQueue[0x40];      // 0x3F0, CContextQueue, built by 0x1806030C0
        std::byte m_state0430[0x50];             // 0x430, built by 0x18099D068
        std::byte m_state0480[0x40];             // 0x480, built by 0x1820F2C10
        void *m_lock04C0;                        // 0x4C0, SRWLOCK
        std::uint64_t m_state04C8;               // 0x4C8
        std::byte m_state04D0[0x80010];          // 0x4D0, built by 0x18099BEB4
        void *m_vector804E0[3];                  // 0x804E0
        std::uint64_t m_state804F8;              // 0x804F8
        std::uint64_t m_state80500;              // 0x80500
        std::uint32_t m_state80508;              // 0x80508
        std::byte m_state8050C[0xC];             // 0x8050C
        std::byte m_state80518[0x50];            // 0x80518, built by 0x180ABB430
        std::uint64_t m_state80568[4];           // 0x80568; 0x80580 seeded from a global
        std::byte m_map80588[0x10];              // 0x80588, an std::map
        void *m_lock80598;                       // 0x80598, SRWLOCK
        std::uint64_t m_state805A0;              // 0x805A0

        static void AssertLayout() {
            static_assert(offsetof(CharacterManager, m_animationImagesLoaded) == 0x8C, "CharacterManager::m_animationImagesLoaded offset mismatch");
            static_assert(offsetof(CharacterManager, m_animationManager) == 0x1C8, "CharacterManager::m_animationManager offset mismatch");
            static_assert(offsetof(CharacterManager, m_pFacialAnimation) == 0x3A0, "CharacterManager::m_pFacialAnimation offset mismatch");
            static_assert(offsetof(CharacterManager, m_arrFrameTimes) == 0x3C0, "CharacterManager::m_arrFrameTimes offset mismatch");
            static_assert(offsetof(CharacterManager, m_state04D0) == 0x4D0, "CharacterManager::m_state04D0 offset mismatch");
        }
    };

    static_assert(sizeof(CharacterManager) == 0x805A8, "CharacterManager size mismatch");
} // namespace SDK
