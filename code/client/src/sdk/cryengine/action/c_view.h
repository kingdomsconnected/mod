#pragma once

#include "../entitysystem/entity_event_listener_with_cleanup.h"
#include "../system/c_camera_native.h"
#include "i_view.h"

#include <cstddef>

namespace SDK {
    class ISystem;

    /**
     * Concrete CryAction camera view.
     *
     * RTTI proves IView at +0x00 and IEntityEventListener at +0x08. The
     * primary table implements all 17 IView slots and appends ProcessShaking
     * and ProcessShake. Construction/destruction and sized deletion prove the
     * complete 0x438-byte layout below.
     */
    class CView : public IView, public IEntityEventListener {
      public:
        /** Native per-shake element; vector stride is exactly 0xD0 bytes. */
        struct SShake {
            std::byte state[0xD0];
        };

        ~CView() override = default;

        virtual void ProcessShaking(float frameTime) = 0; // +0x88
        virtual void ProcessShake(SShake *pShake,
                                  float frameTime) = 0; // +0x90

        EntityId GetLinkedEntityIdRaw() const { return m_linkedEntityId; }
        SViewParams &GetViewParams() { return m_viewParams; }
        const SViewParams &GetViewParams() const { return m_viewParams; }
        CCamera *GetRenderCamera() { return &m_renderCamera; }
        const CCamera *GetRenderCamera() const { return &m_renderCamera; }
        ISystem *GetSystem() const { return m_pSystem; }
        IEntity *GetAudioListenerEntity() const {
            return m_pAudioListenerEntity;
        }
        const Ang3 &GetFrameAdditiveAngles() const {
            return m_frameAdditiveAngles;
        }
        float GetScaleRaw() const { return m_scale; }
        float GetZoomedScaleRaw() const { return m_zoomedScale; }
        std::size_t GetShakeCount() const {
            return m_pShakesBegin != nullptr
                       ? static_cast<std::size_t>(m_pShakesEnd - m_pShakesBegin)
                       : 0;
        }

      private:
        EntityId m_linkedEntityId; // 0x010
        SViewParams m_viewParams; // 0x014
        CCamera m_renderCamera; // 0x0E8
        ISystem *m_pSystem; // 0x3F8
        SShake *m_pShakesBegin; // 0x400
        SShake *m_pShakesEnd; // 0x408
        SShake *m_pShakesCapacity; // 0x410
        IEntity *m_pAudioListenerEntity; // 0x418
        Ang3 m_frameAdditiveAngles; // 0x420
        float m_scale; // 0x42C
        float m_zoomedScale; // 0x430
        std::byte m_padding0434[0x4];

        static void AssertLayout() {
            static_assert(offsetof(CView, m_linkedEntityId) == 0x10, "CView::m_linkedEntityId offset mismatch");
            static_assert(offsetof(CView, m_pAudioListenerEntity) == 0x418, "CView::m_pAudioListenerEntity offset mismatch");
        }
    };

    static_assert(sizeof(CView::SShake) == 0xD0,
                  "CView::SShake size mismatch");
    static_assert(sizeof(CView) == 0x438, "CView size mismatch");
} // namespace SDK
