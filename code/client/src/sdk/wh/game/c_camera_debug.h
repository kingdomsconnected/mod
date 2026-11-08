#pragma once

#include "../../types.h"
#include "c_camera.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::game {
    /** Values returned verbatim by the debug camera's mode-name override. */
    enum class CameraDebugMode : std::uint32_t {
        Free = 0,
        Frozen = 1,
        Orbit = 2,
        Linked = 3,
        Undefined = 4,
    };

    /** Target selector used while CameraDebugMode::Orbit is active. */
    enum class CameraDebugOrbitTarget : std::uint32_t {
        Player = 0,
        Opponent = 1,
        PlayerOpponentBisector = 2,
        Entity = 3,
        CustomPoint = 4,
        Last = 5,
    };

    inline const char *CameraDebugModeToString(CameraDebugMode mode) {
        switch (mode) {
        case CameraDebugMode::Free: return "Free";
        case CameraDebugMode::Frozen: return "Frozen";
        case CameraDebugMode::Orbit: return "Orbit";
        case CameraDebugMode::Linked: return "Linked to a bone";
        case CameraDebugMode::Undefined: return "The game's own";
        }
        return "?";
    }

    inline const char *CameraDebugOrbitTargetToString(CameraDebugOrbitTarget target) {
        switch (target) {
        case CameraDebugOrbitTarget::Player: return "Player";
        case CameraDebugOrbitTarget::Opponent: return "Opponent";
        case CameraDebugOrbitTarget::PlayerOpponentBisector: return "Between the two";
        case CameraDebugOrbitTarget::Entity: return "Followed entity";
        case CameraDebugOrbitTarget::CustomPoint: return "Custom point";
        case CameraDebugOrbitTarget::Last: return "?";
        }
        return "?";
    }

    /**
     * Which of the four transform states the camera keeps, as `SetMode`
     * (0x1839BB944) selects them into the active-state pointer at +0x18.
     *
     * Not the same axis as CameraDebugMode, and deliberately not in the same
     * order: mode Free takes state 0, mode Linked takes state 2 (+0x78) and
     * mode Orbit takes state 3 (+0xA4), while mode Frozen selects nothing and
     * keeps whatever was active. State 1 (+0x4C) is never active; it is where
     * SetMode stashes the free pose on the way out of Free, and where the
     * Free-to-Free path reads it back from.
     */
    enum class CameraDebugTransformState : std::size_t {
        Free      = 0,
        FreeSaved = 1,
        Linked    = 2,
        Orbit     = 3,

        Count     = 4,
    };

    /**
     * One transform the debug camera keeps. The field names come from
     * OnInputEvent: mouse X rotates the second quaternion about +Z and mouse Y
     * the first about +X.
     */
    struct CameraDebugTransformStateLayout {
        SDK::Quat m_pitchAboutX; // 0x00
        SDK::Quat m_yawAboutZ;   // 0x10
        SDK::Vec3 m_position;    // 0x20
    };

    static_assert(sizeof(CameraDebugTransformStateLayout) == 0x2C, "CameraDebugTransformStateLayout size mismatch");

    /**
     * The game's own free / frozen / orbit / bone-linked camera, embedded in
     * C_CameraManager at +0x230. UpdateView drives all four modes and
     * OnInputEvent gives it mouse look, so driving it is a mode plus this
     * state -- nothing has to compute a camera.
     *
     * Table 0x183A5A4C0, 13 entries; the deleting destructor proves 0xE8.
     */
    class C_CameraDebug : public C_Camera {
      public:
        ~C_CameraDebug() override = default;

        void unk_fn0001(SViewParams &) override {}
        const char *unk_fn0006() const override { return "Debug"; }

        /**
         * Switches mode and does that mode's transition work: seeding the free
         * pose, raycasting a custom orbit point, or cycling the orbit target.
         * Routing the view is the manager's job, not this.
         */
        void SetMode(CameraDebugMode mode);

        /** Places the orbit camera: yaw and pitch in radians, then distance. */
        void SetOrbitPosition(const SDK::Vec3 &yawPitchDistance);

        /** Seeds the free pose from the render camera, once. */
        void EnsureInitialized();

        CameraDebugMode GetMode() const { return m_mode; }
        CameraDebugOrbitTarget GetOrbitTarget() const { return m_orbitTarget; }
        void SetOrbitTarget(CameraDebugOrbitTarget target) { m_orbitTarget = target; }

        const CameraDebugTransformStateLayout *GetActiveTransformState() const { return m_pActiveTransformState; }

        /**
         * What orbit target CustomPoint circles. With an entity id set the
         * point is in that entity's local space, which is how the camera
         * orbits a body without reaching outside this class for the subject.
         */
        const SDK::Vec3 &GetCustomOrbitPoint() const { return m_customOrbitPoint; }
        void SetCustomOrbitPoint(const SDK::Vec3 &point) { m_customOrbitPoint = point; }
        SDK::EntityId GetCustomOrbitEntityId() const { return m_customOrbitEntityId; }
        void SetCustomOrbitEntityId(SDK::EntityId entityId) { m_customOrbitEntityId = entityId; }
        bool IsInitialized() const { return m_initialized; }

        CameraDebugTransformStateLayout *GetTransformState(CameraDebugTransformState state) { return state < CameraDebugTransformState::Count ? &m_transformStates[static_cast<std::size_t>(state)] : nullptr; }
        const CameraDebugTransformStateLayout *GetTransformState(CameraDebugTransformState state) const { return state < CameraDebugTransformState::Count ? &m_transformStates[static_cast<std::size_t>(state)] : nullptr; }

      private:
        CameraDebugMode m_mode;                                  // 0x10
        CameraDebugOrbitTarget m_orbitTarget;                    // 0x14
        CameraDebugTransformStateLayout *m_pActiveTransformState; // 0x18
        CameraDebugTransformStateLayout m_transformStates[static_cast<std::size_t>(CameraDebugTransformState::Count)]; // 0x20
        SDK::Vec3 m_customOrbitPoint;                            // 0xD0
        SDK::EntityId m_customOrbitEntityId;                     // 0xDC
        bool m_initialized;                                      // 0xE0
        std::byte m_padding00E1[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_CameraDebug, m_mode) == 0x10, "C_CameraDebug::m_mode offset mismatch");
            static_assert(offsetof(C_CameraDebug, m_pActiveTransformState) == 0x18, "C_CameraDebug active-state offset mismatch");
            static_assert(offsetof(C_CameraDebug, m_transformStates) == 0x20, "C_CameraDebug transform-state offset mismatch");
            static_assert(offsetof(C_CameraDebug, m_customOrbitPoint) == 0xD0, "C_CameraDebug custom-point offset mismatch");
            static_assert(offsetof(C_CameraDebug, m_initialized) == 0xE0, "C_CameraDebug initialized offset mismatch");
        }
    };

    static_assert(sizeof(CameraDebugMode) == 0x4, "CameraDebugMode size mismatch");
    static_assert(sizeof(CameraDebugOrbitTarget) == 0x4, "CameraDebugOrbitTarget size mismatch");
    static_assert(sizeof(C_CameraDebug) == 0xE8, "C_CameraDebug size mismatch");
} // namespace SDK::wh::game
