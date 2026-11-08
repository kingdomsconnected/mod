#pragma once

#include "../animation/i_character_instance.h"
#include "../animation/i_skeleton_anim.h"
#include "../animation/i_skeleton_pose.h"
#include "i_action_controller.h"
#include "i_animated_character.h"
#include "i_animation_graph_state.h"
#include "s_animated_character_params.h"
#include "../../types.h"
#include "../../wh/shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** One loaded Mannequin animation database (.adb). Opaque. */
    struct IAnimationDatabase;

    /**
     * Concrete CryAction animated-character extension.
     *
     * RTTI proves the global CAnimatedCharacter -> IAnimatedCharacter
     * hierarchy. Its primary table at 0x183A49468 contains exactly the 115
     * interface slots, while the IAnimationGraphStateListener secondary table
     * is installed at +0x40. The creation path allocates exactly 0x858 bytes.
     *
     * KCD2 animates actors with Mannequin and keeps CryAction's animation
     * graph only as a facade over it, so both live here side by side. The
     * setup routine at 0x18093F108 reads the entity class script for
     * `ActionController`, `AnimDatabase3P`, `AnimDatabase1P`, `SoundDatabase`
     * and `UseMannequinAGState`, and writes each result into the members
     * below; that is what proves them. The animation graph state is proven by
     * the same routine registering this object's own +0x40 listener sub-object
     * with it under the name "animchar".
     */
    class CAnimatedCharacter: public IAnimatedCharacter {
      public:
        ~CAnimatedCharacter() override = default;

        /**
         * Mannequin's controller for this actor, or null before the entity's
         * init event has built one.
         *
         * Its scope contexts are what a character instance is bound to;
         * without one, nothing this class holds can animate anything.
         */
        IActionController *GetActionController() const {
            return m_pActionController;
        }

        /** The third-person database the `Char3P` scope context is bound with. */
        IAnimationDatabase *GetAnimDatabase3P() const {
            return m_pAnimDatabase3P;
        }

        /**
         * The animation graph facade, present only while the entity class sets
         * `UseMannequinAGState`. Its inputs are translated into Mannequin tags
         * and fragments.
         */
        IAnimationGraphState *GetAnimationGraphStateRuntime() const {
            return m_pAnimationGraphState;
        }

        /**
         * The character instance Mannequin is currently bound to, which is a
         * cache of `IEntity::GetCharacter(0)` taken the last time
         * RefreshCharacterBindings ran -- not what the entity holds now.
         * The two differing is exactly the case that call exists to fix.
         */
        ICharacterInstance *GetBoundCharacter3P() const {
            return m_pCharacter3P;
        }

        ISkeletonAnim *GetBoundSkeletonAnim3P() const {
            return m_pSkeletonAnim3P;
        }

        bool UsesMannequinAnimationGraphState() const {
            return m_useMannequinAGState;
        }

        /**
         * The movement control methods in force, horizontal and vertical: the
         * "current" slot (2) of the per-slot tables `UpdateMCMComponent`
         * resolves (`GetMCMH`/`GetMCMV`, slots 75/76, read +0x5BC/+0x5D0).
         * `EMovementControlMethod`: 1 Entity, 2 Animation, 6
         * AnimationHCollision.
         */
        std::int32_t GetCurrentMCMH() const {
            return m_mcmHorizontal[2];
        }
        std::int32_t GetCurrentMCMV() const {
            return m_mcmVertical[2];
        }

        /** `EColliderMode` the living collider is in; 5 is PushesPlayersOnly. */
        std::int32_t GetColliderMode() const {
            return m_colliderMode;
        }

        /** Whether `ForceMovement` was used last frame (copied from +0x1B8 by UpdatePhysicalEntityMovement 0x1808A3C29). */
        bool WasMovementForced() const {
            return m_movementForcedLastFrame;
        }

        /**
         * Whether `UpdatePhysicsInertia` (0x1808A3C84) hands the living entity
         * zero inertia this frame: unless +0x838 bypasses it, forced movement,
         * an animation-driven vertical method, an animation-driven horizontal
         * one, or the PushesPlayersOnly collider mode zero it. With zero
         * inertia CryPhysics sets the velocity straight to the request, on the
         * ground and in the air alike.
         */
        bool ZeroesInertia() const {
            const std::int32_t horizontal = GetCurrentMCMH();
            return !m_inertiaKept && (m_movementForcedLastFrame || GetCurrentMCMV() == 2 || horizontal == 2 || horizontal == 6 || m_colliderMode == 5);
        }

      private:
        void *m_pAnimationGraphStateListenerVtable; // 0x40
        // From here the members follow CryAction's CAnimatedCharacter in
        // order, anchored by the constructor (0x180704504) and the offsets
        // pinned below; KCD2 adds the signal in front of the listeners.
        SDK::wh::shared::C_Signal m_signal0048; // 0x48
        std::byte m_listeners[0x28];            // 0x58, TAnimCharListeners, built by 0x1807046CC
        IActionController *m_pActionController; // 0x80
        void *m_pAnimContext;                   // 0x88, SAnimationContext
        IAnimationDatabase *m_pAnimDatabase1P;  // 0x90
        IAnimationDatabase *m_pAnimDatabase3P;  // 0x98
        IAnimationDatabase *m_pSoundDatabase;   // 0xA0
        // Set when something other than a character swap has invalidated the
        // scope contexts; RefreshCharacterBindings re-binds on either.
        bool m_scopeContextsDirty; // 0xA8
        std::byte m_padding00A9[0x7];
        SDK::CryStringT<char> m_agScopeMask; // 0xB0
        IAnimationGraphState *m_pAnimationGraphState; // 0xB8
        void *m_pAnimationPlayerProxies[2]; // 0xC0, per animation-graph layer
        std::byte m_opaque00D0[0x8];        // 0xD0
        // GetParams (slot 59) returns this+0xD8; UpdatePhysicsInertia
        // (0x1808A3C84) reads +0xDC/+0xE0/+0xE4 and ResetVars (0x18070470C)
        // clears +0xD8/+0xE0/+0xF0 and sets +0xE8 to 0.3.
        SAnimatedCharacterParams m_params; // 0xD8
        // What UpdatePhysicsInertia last gave the living entity;
        // ResetInertiaCache (0x1809CEB88) reloads it from the entity.
        float m_prevInertia;                // 0xF8
        float m_prevInertiaAccel;           // 0xFC
        float m_prevTimeImpulseRecover;     // 0x100
        std::uint32_t m_state0104;          // 0x104, ResetVars zeroes it
        bool m_moveOverrideUseAnimXY;       // 0x108
        bool m_moveOverrideUseAnimZ;        // 0x109
        bool m_moveOverrideUseAnimRot;      // 0x10A
        std::byte m_padding010B[0x1];
        std::int32_t m_facialAlertness;     // 0x10C
        std::int32_t m_currentStance;       // 0x110
        std::int32_t m_requestedStance;     // 0x114
        std::uint32_t m_stanceQuery;        // 0x118
        std::uint32_t m_disallowLookIKFlags; // 0x11C
        std::int32_t m_curFrameID;          // 0x120
        std::int32_t m_lastResetFrameId;    // 0x124
        std::int32_t m_updateGrabbedInputFrameID; // 0x128
        std::int32_t m_moveRequestFrameID;  // 0x12C
        std::int32_t m_lastAnimProcFrameID; // 0x130
        std::int32_t m_lastSerializeReadFrameID; // 0x134
        std::int32_t m_lastPostProcessUpdateFrameID; // 0x138
        // The entity character slot holding the shadow body, read only while
        // the flag beside it is set.
        int m_shadowCharacterSlot;  // 0x13C
        bool m_hasShadowCharacter;  // 0x140
        bool m_simpleMovementSetOnce;       // 0x141
        std::byte m_padding0142[0x2];
        std::int32_t m_curWeaponRaisedPose; // 0x144
        SCharacterMoveRequest m_moveRequest; // 0x148
        bool m_isPlayer;                    // 0x198
        // Frame times, forced movement and the entity-location QuatTs; the
        // engine's order is known but KCD2's packing here is not pinned.
        std::byte m_opaque0199[0xEB];
        SDK::Vec2 m_reqLocalEntAxx[32];     // 0x284
        SDK::Vec2 m_reqEntVelo[32];         // 0x384
        std::int64_t m_reqEntTime[32];      // 0x488, CTimeValue
        std::int32_t m_reqLocalEntAxxNextIndex; // 0x588
        SDK::Vec3 m_smoothedActualEntVelo;  // 0x58C
        float m_smoothedAmountAxx;          // 0x598
        SDK::Vec3 m_avgLocalEntAxx;         // 0x59C
        bool m_doMotionParams;              // 0x5A8
        std::int8_t m_requestedEntMoveDirLH4; // 0x5A9
        std::int8_t m_actualEntMoveDirLH4;  // 0x5AA
        std::byte m_opaque05AB[0x9];
        std::int32_t m_mcmHorizontal[5]; // 0x5B4, per EMCMSlot: game, animation, current, previous, debug
        std::int32_t m_mcmVertical[5];   // 0x5C8
        std::byte m_opaque05DC[0x98];
        std::int32_t m_colliderMode; // 0x674
        std::byte m_opaque0678[0x60];
        ICharacterInstance *m_pCharacter3P; // 0x6D8
        ISkeletonAnim *m_pSkeletonAnim3P;   // 0x6E0
        ISkeletonPose *m_pSkeletonPose3P;   // 0x6E8
        ICharacterInstance *m_pCharacter1P; // 0x6F0
        ISkeletonAnim *m_pSkeletonAnim1P;   // 0x6F8
        ISkeletonPose *m_pSkeletonPose1P;   // 0x700
        std::byte m_opaque0708[0x12C];
        std::uint8_t m_state0834;    // 0x834
        bool m_useMannequinAGState;  // 0x835
        std::byte m_opaque0836[0x1];
        bool m_movementForcedLastFrame; // 0x837
        bool m_inertiaKept;             // 0x838
        std::byte m_opaque0839[0x1F];

        // Never called. The offsets are what make the members above usable, so
        // they are pinned here, where the type is complete and its own members
        // are in scope.
        static void AssertLayout() {
            static_assert(offsetof(CAnimatedCharacter, m_pActionController) == 0x80, "CAnimatedCharacter::m_pActionController offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_pAnimDatabase1P) == 0x90, "CAnimatedCharacter::m_pAnimDatabase1P offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_pAnimDatabase3P) == 0x98, "CAnimatedCharacter::m_pAnimDatabase3P offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_pSoundDatabase) == 0xA0, "CAnimatedCharacter::m_pSoundDatabase offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_scopeContextsDirty) == 0xA8, "CAnimatedCharacter::m_scopeContextsDirty offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_pAnimationGraphState) == 0xB8, "CAnimatedCharacter::m_pAnimationGraphState offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_shadowCharacterSlot) == 0x13C, "CAnimatedCharacter::m_shadowCharacterSlot offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_agScopeMask) == 0xB0, "CAnimatedCharacter::m_agScopeMask offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_params) == 0xD8, "CAnimatedCharacter::m_params offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_prevInertia) == 0xF8, "CAnimatedCharacter::m_prevInertia offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_facialAlertness) == 0x10C, "CAnimatedCharacter::m_facialAlertness offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_lastPostProcessUpdateFrameID) == 0x138, "CAnimatedCharacter::m_lastPostProcessUpdateFrameID offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_moveRequest) == 0x148, "CAnimatedCharacter::m_moveRequest offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_reqLocalEntAxx) == 0x284, "CAnimatedCharacter::m_reqLocalEntAxx offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_reqEntTime) == 0x488, "CAnimatedCharacter::m_reqEntTime offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_doMotionParams) == 0x5A8, "CAnimatedCharacter::m_doMotionParams offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_pCharacter3P) == 0x6D8, "CAnimatedCharacter::m_pCharacter3P offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_pCharacter1P) == 0x6F0, "CAnimatedCharacter::m_pCharacter1P offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_useMannequinAGState) == 0x835, "CAnimatedCharacter::m_useMannequinAGState offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_mcmHorizontal) == 0x5B4, "CAnimatedCharacter::m_mcmHorizontal offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_mcmVertical) == 0x5C8, "CAnimatedCharacter::m_mcmVertical offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_colliderMode) == 0x674, "CAnimatedCharacter::m_colliderMode offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_movementForcedLastFrame) == 0x837, "CAnimatedCharacter::m_movementForcedLastFrame offset mismatch");
            static_assert(offsetof(CAnimatedCharacter, m_inertiaKept) == 0x838, "CAnimatedCharacter::m_inertiaKept offset mismatch");
        }
    };

    static_assert(sizeof(CAnimatedCharacter) == 0x858, "CAnimatedCharacter size mismatch");
} // namespace SDK
