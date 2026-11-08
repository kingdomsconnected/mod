#pragma once

#include "../../cryengine/action/s_view_params.h"
#include "../../cryengine/system/i_system_event_listener.h"
#include "../framework/c_screen_effects.h"
#include "c_camera_bone_animated.h"
#include "c_camera_combat_delegate.h"
#include "c_camera_debug.h"
#include "c_camera_dialog.h"
#include "c_camera_first_person.h"
#include "c_camera_minigame.h"
#include "c_camera_rider.h"
#include "c_camera_third_person.h"
#include "c_camera_ui.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::game {
    /**
     * Owns the game's cameras and routes the view to one of them.
     *
     * Two pointers route it, and they are not the same thing. The constructor
     * (0x180896DC8) builds each camera at the offset below, points
     * m_pDrivingCamera at the third-person one and m_pSelectedCamera at the
     * first-person one. UpdateCameraSelection (0x1809E58CC), run every frame
     * from C_Player::Update whichever view is drawn, re-points only
     * m_pSelectedCamera, in priority order: the UI camera while an Apse page
     * is up, then bone-animated, combat delegate, dialog, minigame, rider,
     * and first person last. SetDebugCameraMode (0x1839BAEB8) re-points only
     * m_pDrivingCamera, to the debug camera.
     *
     * Whatever it picks reaches the screen only through the player's own view:
     * an inactive CView is not updated at all (CView::Update returns early).
     */
    class C_CameraManager : public ISystemEventListener {
      public:
        ~C_CameraManager() override = default;

        /**
         * Slot 3, 0x1807EEDCC. Stores `*suppressed != 0` and answers true.
         *
         * UpdatePlayerFirstPersonView (0x18082EC48), run from every manager
         * update, puts the local player in first-person view state only while
         * this is clear -- and re-applies C_Human::SetFirstPersonViewState
         * whenever the two disagree. So this, not a one-off call to that, is
         * what keeps the head and the clothing the FirstPersonView group hides
         * drawn: for a camera that films the player from outside their eyes.
         * No direct caller is known; the game reaches it through the vtable.
         */
        virtual bool SetFirstPersonViewSuppressed(const std::int32_t *suppressed) = 0;

        /** Returns the game camera manager published through gEnv. */
        static C_CameraManager *GetInstance();

        /**
         * Routes the view to the debug camera, or hands it back on Undefined.
         * Every other mode is behind a cheat gate and a refusal is silent, so
         * read IsDebugCameraActive back rather than assuming it took.
         */
        void SetDebugCameraMode(CameraDebugMode mode);

        /** The same transition without that gate. Skips the toggle signal. */
        void ForceDebugCameraMode(CameraDebugMode mode);

        bool IsDebugCameraActive() const { return m_debugCameraActive; }

        /** What SetFirstPersonViewSuppressed last stored. */
        bool IsFirstPersonViewSuppressed() const { return m_firstPersonViewSuppressed != 0; }

        /** The third-person camera, or the debug camera while that is on. */
        C_Camera *GetDrivingCamera() const { return m_pDrivingCamera; }

        /** The gameplay camera UpdateCameraSelection picked this frame. */
        C_Camera *GetSelectedCamera() const { return m_pSelectedCamera; }

        /**
         * Whether an Apse page -- the map, inventory, journal, crafting or
         * codex -- is filming its 3D scene through the UI camera.
         */
        bool IsUICameraActive() const { return m_pSelectedCamera == &m_uiCamera; }

        C_CameraFirstPerson *GetFirstPersonCamera() { return &m_firstPersonCamera; }
        C_CameraThirdPerson *GetThirdPersonCamera() { return &m_thirdPersonCamera; }
        C_CameraDebug *GetDebugCamera() { return &m_debugCamera; }
        C_CameraDialog *GetDialogCamera() { return &m_dialogCamera; }
        C_CameraMinigame *GetMinigameCamera() { return &m_minigameCamera; }
        C_CameraBoneAnimated *GetBoneAnimatedCamera() { return &m_boneAnimatedCamera; }
        C_CameraCombatDelegate *GetCombatDelegateCamera() { return &m_combatDelegateCamera; }
        C_CameraUI *GetUICamera() { return &m_uiCamera; }
        C_CameraRider *GetRiderCamera() { return &m_riderCamera; }

        SViewParams &GetViewParams003C() { return m_viewParams003C; }
        const SViewParams &GetViewParams003C() const { return m_viewParams003C; }
        SViewParams &GetViewParams0110() { return m_viewParams0110; }
        const SViewParams &GetViewParams0110() const { return m_viewParams0110; }

        const C_CameraFirstPerson *GetFirstPersonCamera() const { return &m_firstPersonCamera; }
        const C_CameraThirdPerson *GetThirdPersonCamera() const { return &m_thirdPersonCamera; }
        const C_CameraDebug *GetDebugCamera() const { return &m_debugCamera; }
        const C_CameraDialog *GetDialogCamera() const { return &m_dialogCamera; }
        const C_CameraMinigame *GetMinigameCamera() const { return &m_minigameCamera; }
        const C_CameraBoneAnimated *GetBoneAnimatedCamera() const { return &m_boneAnimatedCamera; }
        const C_CameraCombatDelegate *GetCombatDelegateCamera() const { return &m_combatDelegateCamera; }
        const C_CameraUI *GetUICamera() const { return &m_uiCamera; }
        const C_CameraRider *GetRiderCamera() const { return &m_riderCamera; }

      private:
        std::byte m_signal0008[0x10]; // 0x08
        std::byte m_signal0018[0x10]; // 0x18, fired when the debug camera toggles

        C_Camera *m_pDrivingCamera; // 0x28
        C_Camera *m_pSelectedCamera; // 0x30
        bool m_debugCameraActive; // 0x38
        std::uint8_t m_firstPersonViewSuppressed; // 0x39, the ctor (0x180896DC8) zeroes the dword at 0x38
        std::uint8_t m_switched003A; // 0x3A, UpdateCameraSelection sets it on a switch
        std::uint8_t m_state003B; // 0x3B
        SViewParams m_viewParams003C; // 0x3C
        SViewParams m_viewParams0110; // 0x110
        std::byte m_padding01E4[0x4];
        C_CameraFirstPerson m_firstPersonCamera; // 0x1E8
        C_CameraThirdPerson m_thirdPersonCamera; // 0x208
        C_CameraDebug m_debugCamera; // 0x230
        C_CameraDialog m_dialogCamera; // 0x318
        C_CameraMinigame m_minigameCamera; // 0x338
        C_CameraBoneAnimated m_boneAnimatedCamera; // 0x350
        C_CameraCombatDelegate m_combatDelegateCamera; // 0x370
        C_CameraUI m_uiCamera; // 0x390
        C_CameraRider m_riderCamera; // 0x3C0
        std::byte m_callback0428[0x50];
        framework::CScreenEffects m_screenEffects; // 0x478, built by 0x180897038

        static void AssertLayout() {
            static_assert(offsetof(C_CameraManager, m_pDrivingCamera) == 0x28, "C_CameraManager driving-camera offset mismatch");
            static_assert(offsetof(C_CameraManager, m_pSelectedCamera) == 0x30, "C_CameraManager selected-camera offset mismatch");
            static_assert(offsetof(C_CameraManager, m_uiCamera) == 0x390, "C_CameraManager UI-camera offset mismatch");
            static_assert(offsetof(C_CameraManager, m_debugCameraActive) == 0x38, "C_CameraManager debug-active offset mismatch");
            static_assert(offsetof(C_CameraManager, m_viewParams003C) == 0x3C, "C_CameraManager view-params offset mismatch");
            static_assert(offsetof(C_CameraManager, m_debugCamera) == 0x230, "C_CameraManager debug-camera offset mismatch");
            static_assert(offsetof(C_CameraManager, m_riderCamera) == 0x3C0, "C_CameraManager rider-camera offset mismatch");
            static_assert(offsetof(C_CameraManager, m_firstPersonViewSuppressed) == 0x39, "C_CameraManager first-person-suppressed offset mismatch");
            static_assert(offsetof(C_CameraManager, m_switched003A) == 0x3A, "C_CameraManager switched offset mismatch");
            static_assert(offsetof(C_CameraManager, m_screenEffects) == 0x478, "C_CameraManager screen-effects offset mismatch");
        }
    };

    static_assert(sizeof(C_CameraManager) == 0x4D0, "C_CameraManager size mismatch");
} // namespace SDK::wh::game
