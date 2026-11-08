#pragma once

#include "../../types.h"
#include "c_combat_actor_updated_object.h"
#include "c_combat_camera_animate.h"
#include "c_combat_camera_attack.h"
#include "c_combat_camera_bone.h"
#include "c_combat_camera_death.h"
#include "c_combat_camera_guard.h"
#include "c_combat_camera_hit_reaction.h"
#include "i_combat_camera_manager.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /** Native 0x50-byte scalar camera controller/callback state. */
    struct CombatCameraScalarControllerLayout {
        std::byte m_state[0x50];
    };

    /** Native 0x60-byte vector camera controller/callback state. */
    struct CombatCameraVectorControllerLayout {
        std::byte m_state[0x60];
    };

    static_assert(sizeof(CombatCameraScalarControllerLayout) == 0x50,
                  "CombatCameraScalarControllerLayout size mismatch");
    static_assert(sizeof(CombatCameraVectorControllerLayout) == 0x60,
                  "CombatCameraVectorControllerLayout size mismatch");

    /**
     * Update-aware owner and coordinator for combat cameras.
     *
     * Construction at 0x180A4A4AC allocates the six concrete cameras exposed
     * below. Destruction walks exactly those six pointers and releases four
     * independently constructed callback/controller blocks. The constructor's
     * owner parameter type remains insufficiently proven and is left unnamed.
     */
    class C_CombatCameraManager : public C_CombatActorUpdatedObject,
                                  public I_CombatCameraManager {
      public:
        ~C_CombatCameraManager() override = default;

        C_CombatCameraGuard *GetGuardCamera() const { return m_pGuardCamera; }
        C_CombatCameraAttack *GetAttackCamera() const { return m_pAttackCamera; }
        C_CombatCameraHitReaction *GetHitReactionCamera() const {
            return m_pHitReactionCamera;
        }
        C_CombatCameraDeath *GetDeathCamera() const { return m_pDeathCamera; }
        C_CombatCameraAnimate *GetAnimateCamera() const { return m_pAnimateCamera; }
        C_CombatCameraBone *GetBoneCamera() const { return m_pBoneCamera; }
        C_CombatCamera *GetCamera0058Raw() const { return m_pCamera0058; }

        /**
         * The rotation of this fighter's combat camera: identity from the
         * constructor (0x180A4A4AC). The player's combat input listener
         * (`C_CombatPlayerInput::FilterMovementRequest`, 0x180830BCC) seeds
         * its turn from it and, in its free mode, writes its yaw into the
         * body and the view rotation.
         */
        const SDK::Quat &GetRotation() const { return m_rotation; }
        void SetRotation(const SDK::Quat &rotation) { m_rotation = rotation; }

      private:
        bool m_state0020;
        std::byte m_padding0021[0x7];
        C_CombatCameraGuard *m_pGuardCamera;             // 0x28
        C_CombatCameraAttack *m_pAttackCamera;           // 0x30
        C_CombatCameraHitReaction *m_pHitReactionCamera; // 0x38
        C_CombatCameraDeath *m_pDeathCamera;             // 0x40
        C_CombatCameraAnimate *m_pAnimateCamera;         // 0x48
        C_CombatCameraBone *m_pBoneCamera;               // 0x50
        C_CombatCamera *m_pCamera0058;
        SDK::Quat m_rotation; // 0x60
        SDK::Vec3 m_vector0070;
        std::byte m_state007C[0x8];
        std::uint32_t m_state0084;
        float m_state0088;
        std::uint16_t m_state008C;
        std::uint8_t m_state008E;
        std::byte m_padding008F;
        std::uint64_t m_state0090;
        std::uint32_t m_state0098;
        float m_state009C;
        SDK::Vec3 m_vector00A0;
        std::byte m_state00AC[0x8];
        std::uint32_t m_state00B4;
        float m_state00B8;
        std::byte m_padding00BC[0x4];
        std::int64_t m_state00C0;
        void *m_pState00C8;
        void *m_pState00D0;
        CombatCameraScalarControllerLayout m_scalarController00D8;
        CombatCameraVectorControllerLayout m_vectorController0128;
        CombatCameraVectorControllerLayout m_vectorController0188;
        CombatCameraScalarControllerLayout m_scalarController01E8;
    };

    static_assert(sizeof(C_CombatCameraManager) == 0x238,
                  "C_CombatCameraManager size mismatch");
} // namespace SDK::wh::combatmodule
