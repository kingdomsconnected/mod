#pragma once

#include "../../cryengine/action/i_procedural_context.h"
#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * The Mannequin context that turns a body's aim pose toward a target
     * through CProceduralContextAim, vtable 0x183AF1110.
     *
     * Two parties ask for the pose. The game does, through
     * C_Actor::UpdateAimPose (0x180E4A570), which writes the target and its
     * own request; the fragment does, through an "AimPose" procedural clip.
     * `Update` (0x180C4F744) enables the aim blender only while both ask, so
     * a body playing no fragment with that clip holds its pose level however
     * high its target is.
     */
    class CProceduralContextAim: public IProceduralContext {
      public:
        /** Whether the game asks for the aim pose (C_Actor::UpdateAimPose). */
        bool IsGameRequested() const {
            return m_gameRequest;
        }

        /** Whether a playing fragment's AimPose procedural clip asks for it. */
        bool IsClipRequested() const {
            return m_clipRequest;
        }

        /** The world point the game asked the pose to aim at. */
        const Vec3 &GetTarget() const {
            return m_target;
        }

      private:
        // Factory 0x18100BE8C allocates 0x60 bytes.
        void *m_pEntity;           // 0x08, IProceduralContext::m_entity in CryEngine; factory zeroes
        void *m_pActionController; // 0x10, IProceduralContext::m_actionController in CryEngine; factory zeroes
        void *m_pPoseBlenderAim; // 0x18, cached by InitialisePoseBlenderAim (0x180C4F46C)
        bool m_gameRequest;      // 0x20
        bool m_clipRequest;      // 0x21
        std::byte m_padding0022[0x2]; // factory 0x18100BE8C writes only the word at 0x20
        Vec3 m_target; // 0x24
        Vec2 m_defaultMaxSmoothRate;     // 0x30, m_defaultPolarCoordinatesMaxSmoothRateRadiansPerSecond; factory sets 2*pi twice
        float m_defaultSmoothTime;       // 0x38, m_defaultPolarCoordinatesSmoothTimeSeconds; factory sets 0.1
        std::uint8_t m_flag003C;         // 0x3C, factory sets 1
        std::byte m_padding003D[0x3];
        std::uint32_t m_nextRequestId;   // 0x40, CRequestList::m_nextId
        std::byte m_padding0044[0x4];
        std::byte m_smoothingRequests[0x18]; // 0x48, CRequestList's native std::vector (destroyed by 0x18100BE20)

        static void AssertLayout() {
            static_assert(offsetof(CProceduralContextAim, m_pPoseBlenderAim) == 0x18, "CProceduralContextAim::m_pPoseBlenderAim offset mismatch");
            static_assert(offsetof(CProceduralContextAim, m_gameRequest) == 0x20, "CProceduralContextAim::m_gameRequest offset mismatch");
            static_assert(offsetof(CProceduralContextAim, m_clipRequest) == 0x21, "CProceduralContextAim::m_clipRequest offset mismatch");
            static_assert(offsetof(CProceduralContextAim, m_target) == 0x24, "CProceduralContextAim::m_target offset mismatch");
            static_assert(offsetof(CProceduralContextAim, m_pEntity) == 0x08, "CProceduralContextAim::m_pEntity offset mismatch");
            static_assert(offsetof(CProceduralContextAim, m_defaultMaxSmoothRate) == 0x30, "CProceduralContextAim::m_defaultMaxSmoothRate offset mismatch");
            static_assert(offsetof(CProceduralContextAim, m_nextRequestId) == 0x40, "CProceduralContextAim::m_nextRequestId offset mismatch");
            static_assert(offsetof(CProceduralContextAim, m_smoothingRequests) == 0x48, "CProceduralContextAim::m_smoothingRequests offset mismatch");
        }
    };

    static_assert(sizeof(CProceduralContextAim) == 0x60, "CProceduralContextAim size mismatch");
} // namespace SDK::wh::entitymodule
