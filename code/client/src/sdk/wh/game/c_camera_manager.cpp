#include "c_camera_manager.h"

#include "../c_wh_global_environment.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::game {
    C_CameraManager *C_CameraManager::GetInstance() {
        auto *environment = C_GlobalEnvironment::GetInstance();
        return environment != nullptr ? environment->GetCameraManager() : nullptr;
    }

    void C_CameraManager::SetDebugCameraMode(CameraDebugMode mode) {
        hook::this_call<void>(gPatterns.C_CameraManager__SetDebugCameraMode, this, static_cast<std::uint32_t>(mode));
    }

    void C_CameraManager::ForceDebugCameraMode(CameraDebugMode mode) {
        if (mode == CameraDebugMode::Undefined) {
            m_debugCameraActive = false;
            return;
        }

        m_debugCameraActive = true;
        m_debugCamera.SetMode(mode);
        m_debugCamera.EnsureInitialized();
        m_pDrivingCamera = &m_debugCamera;
    }

    void C_CameraDebug::SetMode(CameraDebugMode mode) {
        hook::this_call<void>(gPatterns.C_CameraDebug__SetMode, this, static_cast<std::uint32_t>(mode));
    }

    void C_CameraDebug::SetOrbitPosition(const SDK::Vec3 &yawPitchDistance) {
        hook::this_call<void>(gPatterns.C_CameraDebug__SetOrbitPosition, this, &yawPitchDistance);
    }

    void C_CameraDebug::EnsureInitialized() {
        hook::this_call<void>(gPatterns.C_CameraDebug__EnsureInitialized, this);
    }
} // namespace SDK::wh::game
