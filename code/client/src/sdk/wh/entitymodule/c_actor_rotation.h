#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    class C_Actor;

    /**
     * C_Actor+0x238: a body's facing as the player pipeline keeps it.
     *
     * `Update` (0x1804415E0) runs every frame from C_Actor::PrePhysicsMovement
     * (0x181E7D6F0), after the movement controller's Update and before the
     * ground state's turn. It copies the frame's delta angles out of the
     * movement params, adds the angular impulse, and in `UpdateAngles`
     * (0x180441928) adds the frame delta and the external delta into
     * `m_angles`, clamps them (`ClampAngles`, 0x18053B508) and rebuilds
     * `m_baseQuat` yaw-only from `m_angles.z` -- when the actor has neither
     * a camera limit (C_Actor+0x174) nor a rotation lock (+0x178). A body
     * that is not player controlled takes its angles from the entity
     * instead. `m_viewQuat` is then rebuilt from `m_angles`, and
     * `m_viewQuatFinal` (`UpdateViewFinal`, 0x180441730) adds
     * `m_viewOffsetAngles` on top for a player-controlled actor: that final
     * view is what C_Player::GetViewRotation (0x18094DB70) returns, and so
     * what the movement state's eye direction is built from. The offset
     * and the external delta are zeroed at the end of every Update.
     *
     * `C_ActorStateMovement::ProcessTurning` (0x1809D1D8C) then asks the
     * animated character for entity^-1 * `m_baseQuat` every ground frame,
     * so the base controls a player body's facing. `SetBaseRotation`
     * (0x1806442A8) writes it whatever the limits say and rebuilds the view
     * from it and `m_pitch`; `SetViewRotation` (0x1806440AC) writes the view
     * and the angles always and the base only without limits.
     *
     * The allocation size is not established; the members are pinned.
     */
    class C_ActorRotation {
      public:
        C_Actor *GetActor() const {
            return m_pActor;
        }

        /** The angles the view is built from: x pitch, z yaw, radians. */
        const Ang3 &GetAngles() const {
            return m_angles;
        }

        const Quat &GetViewQuat() const {
            return m_viewQuat;
        }

        /** The view with the frame's offset applied; what GetViewRotation returns. */
        const Quat &GetViewQuatFinal() const {
            return m_viewQuatFinal;
        }

        /** The yaw-only facing the ground state turns the body to. */
        const Quat &GetBaseQuat() const {
            return m_baseQuat;
        }

        /** Angles added into the final view only, this frame; zeroed by Update. */
        const Ang3 &GetViewOffsetAngles() const {
            return m_viewOffsetAngles;
        }

        /** The movement params' delta angles of the last Update. */
        const Ang3 &GetFrameDeltaAngles() const {
            return m_frameDeltaAngles;
        }

        /** A delta queued from outside the movement pass; zeroed by Update. */
        const Ang3 &GetExternalDeltaAngles() const {
            return m_externalDeltaAngles;
        }

      private:
        C_Actor *m_pActor;           // 0x00
        Ang3 m_angles;               // 0x08
        Quat m_viewQuat;             // 0x14
        Quat m_viewQuatFinal;        // 0x24
        Quat m_baseQuat;             // 0x34
        float m_yaw;                 // 0x44, m_angles.z as of the last Update
        float m_pitch;               // 0x48, m_angles.x as of the last Update
        Ang3 m_viewOffsetAngles;     // 0x4C
        std::byte m_opaque0058[0x24]; // 0x58, the angular impulse
        Ang3 m_frameDeltaAngles;     // 0x7C
        Ang3 m_externalDeltaAngles;  // 0x88
        bool m_anglesChanged;        // 0x94

        static void AssertLayout() {
            static_assert(offsetof(C_ActorRotation, m_pActor) == 0x00, "C_ActorRotation::m_pActor offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_angles) == 0x08, "C_ActorRotation::m_angles offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_viewQuat) == 0x14, "C_ActorRotation::m_viewQuat offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_viewQuatFinal) == 0x24, "C_ActorRotation::m_viewQuatFinal offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_baseQuat) == 0x34, "C_ActorRotation::m_baseQuat offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_yaw) == 0x44, "C_ActorRotation::m_yaw offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_pitch) == 0x48, "C_ActorRotation::m_pitch offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_viewOffsetAngles) == 0x4C, "C_ActorRotation::m_viewOffsetAngles offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_frameDeltaAngles) == 0x7C, "C_ActorRotation::m_frameDeltaAngles offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_externalDeltaAngles) == 0x88, "C_ActorRotation::m_externalDeltaAngles offset mismatch");
            static_assert(offsetof(C_ActorRotation, m_anglesChanged) == 0x94, "C_ActorRotation::m_anglesChanged offset mismatch");
        }
    };
} // namespace SDK::wh::entitymodule
