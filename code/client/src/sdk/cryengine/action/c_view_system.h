#pragma once

#include "../movie/i_movie_user.h"
#include "i_level_system.h"
#include "i_view_system.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class ISystem;

    /** MSVC ordered-map state used for view-id to IView ownership. */
    struct ViewSystemViewMapLayout {
        void *head;       // 0x00
        std::size_t size; // 0x08
    };

    static_assert(sizeof(ViewSystemViewMapLayout) == 0x10,
                  "ViewSystemViewMapLayout size mismatch");

    /**
     * Concrete CryAction view registry and movie-camera coordinator.
     *
     * RTTI proves IViewSystem at +0x00, IMovieUser at +0x08, and
     * ILevelSystemListener at +0x10. Construction, destruction, and sized
     * deletion independently prove the complete 0xA8-byte extent.
     */
    class CViewSystem : public IViewSystem,
                        public IMovieUser,
                        public ILevelSystemListener {
      public:
        ~CViewSystem() override = default;

        ISystem *GetSystem() const { return m_pSystem; }
        const ViewSystemViewMapLayout &GetViewMap() const { return m_views; }
        std::uint32_t GetNextViewIdRaw() const {
            return m_nextViewIdToAssign;
        }
        std::uint32_t GetPreSequenceViewIdRaw() const {
            return m_preSequenceViewId;
        }
        std::uint32_t GetCutsceneViewIdRaw() const {
            return m_cutsceneViewId;
        }
        std::uint32_t GetCutsceneCountRaw() const {
            return m_cutsceneCount;
        }
        const Quat &GetOverrideCameraRotationRaw() const {
            return m_overrideCameraRotation;
        }
        bool IsOverrideCameraRotationEnabled() const { return m_overrideCameraRotationEnabled; }
        float GetCameraNoiseRaw() const { return m_cameraNoise; }
        float GetCameraNoiseFrequencyRaw() const {
            return m_cameraNoiseFrequency;
        }
        int GetViewSystemDebugRaw() const { return m_viewSystemDebug; }

      private:
        ISystem *m_pSystem; // 0x18
        ViewSystemViewMapLayout m_views; // 0x20
        std::uint32_t *m_pCutsceneViewIdsBegin; // 0x30
        std::uint32_t *m_pCutsceneViewIdsEnd; // 0x38
        std::uint32_t *m_pCutsceneViewIdsCapacity; // 0x40
        IViewSystemListener **m_pListenersBegin; // 0x48
        IViewSystemListener **m_pListenersEnd; // 0x50
        IViewSystemListener **m_pListenersCapacity; // 0x58
        std::uint32_t m_activeViewId; // 0x60
        std::uint32_t m_nextViewIdToAssign; // 0x64
        std::uint32_t m_preSequenceViewId; // 0x68
        std::uint32_t m_cutsceneViewId; // 0x6C
        std::uint32_t m_cutsceneCount; // 0x70
        bool m_activeViewFromSequence; // 0x74
        bool m_overrideCameraRotationEnabled; // 0x75
        std::byte m_padding0076[0x2];
        Quat m_overrideCameraRotation; // 0x78
        float m_cameraNoise; // 0x88
        float m_cameraNoiseFrequency; // 0x8C
        float m_defaultCameraNearZ; // 0x90
        float m_blendInPositionSpeed; // 0x94
        float m_blendInRotationSpeed; // 0x98
        bool m_performBlendOut; // 0x9C
        std::byte m_padding009D[0x3];
        std::int32_t m_viewSystemDebug; // 0xA0
        bool m_useDeferredViewSystemUpdate; // 0xA4
        bool m_controlsAudioListeners; // 0xA5
        std::byte m_padding00A6[0x2];
    };

    static_assert(sizeof(CViewSystem) == 0xA8,
                  "CViewSystem size mismatch");
} // namespace SDK
