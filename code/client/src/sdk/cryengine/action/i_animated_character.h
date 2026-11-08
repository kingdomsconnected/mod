#pragma once

#include "../../types.h"
#include "../animation/cry_char_animation_params.h"
#include "i_actor.h"
#include "s_animated_character_params.h"
#include "s_ground_alignment_params.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Six final motion-parameter overrides. SetMotionParam (0x1812EF04C)
     * reads the saved store at CAnimatedCharacter +0x168 after smoothing,
     * even under NoMovementOverride. Continuous motion must enter through
     * SetDesiredLocalLocation to retain native smoothing and turn scaling.
     * Layout and insertion are proven by AddMovement (0x1809D2214) and
     * SPredictedCharacterStates::SetParam (0x180949F28).
     */
    struct SPredictedCharacterStates {
        float values[6];        // 0x00
        std::uint8_t ids[6];    // 0x18
        std::uint8_t count = 0; // 0x1E
        std::uint8_t pad001F;   // 0x1F

        bool GetParam(EMotionParamID id, float &value) const {
            const auto raw = static_cast<std::uint8_t>(id);
            for (std::uint8_t i = 0; i < count; ++i) {
                if (ids[i] == raw) {
                    value = values[i];
                    return true;
                }
            }
            return false;
        }

        bool SetParam(EMotionParamID id, float value) {
            const auto raw = static_cast<std::uint8_t>(id);
            for (std::uint8_t i = 0; i < count; ++i) {
                if (ids[i] == raw) {
                    values[i] = value;
                    return true;
                }
            }
            if (count >= 6) {
                return false;
            }
            values[count] = value;
            ids[count]    = raw;
            ++count;
            return true;
        }
    };

    static_assert(sizeof(SPredictedCharacterStates) == 0x20, "SPredictedCharacterStates size mismatch");
    static_assert(offsetof(SPredictedCharacterStates, ids) == 0x18, "SPredictedCharacterStates::ids offset mismatch");
    static_assert(offsetof(SPredictedCharacterStates, count) == 0x1E, "SPredictedCharacterStates::count offset mismatch");

    /**
     * What kind of movement a request is asking for.
     *
     * CryAction's own `eCMT_` set, in its order. `eCMT_None` is load-bearing
     * rather than a placeholder: `AddMovement` treats a zero type as "no
     * request", clearing both the saved request and the prediction count.
     * Only `eCMT_None` and `eCMT_Normal` are reached from this SDK; the rest
     * are listed so the numbering reads, and carry CryAction's values.
     */
    enum ECharacterMoveType : std::int32_t {
        eCMT_None           = 0,
        eCMT_Normal         = 1,
        eCMT_Fly            = 2,
        eCMT_Swim           = 3,
        eCMT_ZeroG          = 4,
        eCMT_Impulse        = 5,
        eCMT_JumpInstant    = 6,
        eCMT_JumpAccumulate = 7,
    };

    static_assert(sizeof(ECharacterMoveType) == 0x4, "ECharacterMoveType size mismatch");

    /**
     * Which of an animated character's movement-control opinions is being
     * stated.
     *
     * `UpdateMCMComponent` (0x1805D0100) proves the whole set: each component
     * owns five consecutive dwords -- horizontal from `+0x5B4`, vertical from
     * `+0x5C8` -- indexed by this enum, and the resolved value is recomputed
     * every frame by letting each later non-zero slot win. So `Game` is the
     * gameplay opinion, `Animation` overrides it, `Cur` and `Prev` are the
     * results the engine writes back, and `Debug` beats everything.
     */
    enum EMCMSlot : std::int32_t {
        eMCMSlot_Game      = 0,
        eMCMSlot_Animation = 1,
        eMCMSlot_Cur       = 2,
        eMCMSlot_Prev      = 3,
        eMCMSlot_Debug     = 4,
    };

    static_assert(sizeof(EMCMSlot) == 0x4, "EMCMSlot size mismatch");

    /** What a living body's physical collider is set to. */
    enum EColliderMode : std::int32_t {
        eColliderMode_Undefined         = 0,
        eColliderMode_Disabled          = 1,
        eColliderMode_GroundedOnly      = 2,
        eColliderMode_Pushable          = 3,
        eColliderMode_NonPushable       = 4,
        eColliderMode_PushesPlayersOnly = 5,
        eColliderMode_Spectator         = 6,
    };

    static_assert(sizeof(EColliderMode) == 0x4, "EColliderMode size mismatch");

    /**
     * Whose opinion of the collider a request states; the highest layer
     * with one wins. KCD2's `UpdatePhysicalEntityMovement` asks on
     * `ForceSleep` (5).
     */
    enum EColliderModeLayer : std::int32_t {
        eColliderModeLayer_AnimGraph  = 0,
        eColliderModeLayer_Game       = 1,
        eColliderModeLayer_Script     = 2,
        eColliderModeLayer_FlowGraph  = 3,
        eColliderModeLayer_Animation  = 4,
        eColliderModeLayer_ForceSleep = 5,
        eColliderModeLayer_Debug      = 6,
    };

    static_assert(sizeof(EColliderModeLayer) == 0x4, "EColliderModeLayer size mismatch");

    /**
     * Who moves the entity: the game's movement request, or the animation.
     *
     * `MergeMCM` (0x1806D49F0) is what gives these meaning, and it survives in
     * KCD2 unchanged. Per component it takes the entity-requested movement for
     * `Entity`, `ClampedEntity` and `SmoothedEntity`, and the animation's own
     * root motion for `Animation`, `DecoupledCatchUp` and
     * `AnimationHCollision`; anything else warns and falls through to the
     * entity. `Undefined` never reaches it -- it means "this slot has no
     * opinion", and `UpdateMCMComponent` skips it.
     */
    enum EMovementControlMethod : std::int32_t {
        eMCM_Undefined           = 0,
        eMCM_Entity              = 1,
        eMCM_Animation           = 2,
        eMCM_DecoupledCatchUp    = 3,
        eMCM_ClampedEntity       = 4,
        eMCM_SmoothedEntity      = 5,
        eMCM_AnimationHCollision = 6,
    };

    static_assert(sizeof(EMovementControlMethod) == 0x4, "EMovementControlMethod size mismatch");

    /**
     * Per-frame movement request. Constructor 0x180705668 proves the layout
     * and x/y/z/w quaternion order. A zero type clears the saved request and
     * prediction count in AddMovement.
     */
    struct SCharacterMoveRequest {
        ECharacterMoveType type;              // 0x00
        Vec3 velocity;                        // 0x04
        Quat rotation;                        // 0x10
        SPredictedCharacterStates prediction; // 0x20
        bool allowStrafe;                     // 0x40
        std::byte pad0041[0x3];
        float proceduralLeaning; // 0x44
        bool jumping;            // 0x48
        std::byte pad0049[0x7];
    };

    static_assert(sizeof(SCharacterMoveRequest) == 0x50, "SCharacterMoveRequest size mismatch");
    static_assert(offsetof(SCharacterMoveRequest, velocity) == 0x04, "SCharacterMoveRequest::velocity offset mismatch");
    static_assert(offsetof(SCharacterMoveRequest, rotation) == 0x10, "SCharacterMoveRequest::rotation offset mismatch");
    static_assert(offsetof(SCharacterMoveRequest, prediction) == 0x20, "SCharacterMoveRequest::prediction offset mismatch");
    static_assert(offsetof(SCharacterMoveRequest, allowStrafe) == 0x40, "SCharacterMoveRequest::allowStrafe offset mismatch");
    static_assert(offsetof(SCharacterMoveRequest, proceduralLeaning) == 0x44, "SCharacterMoveRequest::proceduralLeaning offset mismatch");
    static_assert(offsetof(SCharacterMoveRequest, jumping) == 0x48, "SCharacterMoveRequest::jumping offset mismatch");

    /**
     * CryAction animated-character game-object extension.
     *
     * RTTI identifies this global type as a struct derived from
     * IGameObjectExtension. CAnimatedCharacter's primary table at
     * 0x183A058E8 contains exactly 115 slots: the 29 inherited extension
     * entries followed by 86 IAnimatedCharacter operations. Stripped
     * contracts stay opaque until independently proven.
     */
    struct IAnimatedCharacter: public IGameObjectExtension {
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        /**
         * Slot 33 (0x1809CEE64): resets the animation graph and the action
         * controller (ending every action), then this character's own state.
         * `C_Actor::Revive` runs it before `SetupActorModel`, which is the
         * order a body is rebuilt in.
         */
        virtual void ResetState() = 0; // +0x108
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;

        /**
         * Re-reads the entity's character slots and re-binds Mannequin to them.
         *
         * Caches the third- and first-person character instances with their
         * skeleton animation and pose, then hands the third-person one to the
         * action controller as the `Char3P` scope context -- or clears that
         * context when the slot is empty. Nothing else re-binds: an entity
         * whose character is loaded after this last ran keeps a cleared
         * context, and its body stays in the bind pose.
         *
         * CAnimatedCharacter::ProcessEvent calls this on the entity's init
         * event, immediately after building the action controller, which is
         * what pins the slot.
         *
         * Not idempotent. Every call strips character flag 0x4000 from the
         * previously bound instance and only puts it back when the instance
         * actually changed, so call it when the character changed and not on
         * a timer.
         */
        virtual void RefreshCharacterBindings() = 0; // +0x150, slot 42
        /**
         * Copies a nonzero-type request; zero clears it. NoMovementOverride
         * clears acquired velocity/rotation in 0x18083A604, so puppets supply
         * animation motion through the later SetDesiredLocalLocation hook.
         */
        virtual void AddMovement(const SCharacterMoveRequest &request) = 0; // +0x158, slot 43
        virtual void unk_fn0044()                                      = 0;
        /**
         * The motion parameters the next fragment selection reads:
         * `CAnimatedCharacter+0x168`. `CAnimActionAIMovement::SelectState`
         * (0x1804ADA10) takes the standing turn angle from here
         * (`eMotionParamID_TurnAngle`) to choose idle, turn or big turn.
         */
        virtual const SPredictedCharacterStates &GetOverriddenMotionParameters() const      = 0; // +0x168, slot 45
        virtual void SetOverriddenMotionParameters(const SPredictedCharacterStates &states) = 0; // +0x170, slot 46
        virtual void unk_fn0047()                                      = 0;
        virtual void unk_fn0048()                                      = 0;
        virtual void unk_fn0049()                                      = 0;
        virtual void unk_fn0050()                                      = 0;
        virtual float GetEntitySpeedHorizontal() const                 = 0; // +0x198, slot 51, 0x181670510
        virtual void unk_fn0052()                                      = 0;
        virtual void unk_fn0053()                                      = 0;
        virtual void unk_fn0054()                                      = 0;
        virtual void unk_fn0055()                                      = 0;
        virtual void unk_fn0056()                                      = 0;
        virtual void unk_fn0057()                                      = 0;
        virtual void unk_fn0058()                                      = 0;
        virtual const SAnimatedCharacterParams &GetParams()            = 0; // slot 59, 0x181AA49D0 (COMDAT folded), returns this+0xD8
        virtual void SetParams(const SAnimatedCharacterParams &params) = 0; // slot 60, 0x1808AC370, copies 0x20 bytes
        virtual SGroundAlignmentParams &GetGroundAlignmentParams() = 0; // slot 61, +0x1E8; CryEngine order after SetParams
        virtual void unk_fn0062()                                      = 0;
        virtual void unk_fn0063()                                      = 0;
        virtual void unk_fn0064()                                      = 0;
        virtual void unk_fn0065()                                      = 0;
        virtual void unk_fn0066()                                      = 0;
        virtual void unk_fn0067()                                      = 0;
        virtual void unk_fn0068()                                      = 0;
        /**
         * States one layer's opinion of the body's collider (0x180704040):
         * stores it and the tag per layer and, when it changed, rebuilds the
         * collider (`UpdatePhysicalColliderMode`, 0x18083C630). The tag is
         * retained by pointer, so pass a string literal.
         */
        virtual void RequestPhysicalColliderMode(EColliderMode mode, EColliderModeLayer layer, const char *tag) = 0; // +0x228, slot 69
        virtual void unk_fn0070()                                      = 0;
        /**
         * States one slot's opinion of who moves this body, and why.
         *
         * Writes the pair into `m_movementControlMethod[slot]` and the reason
         * string beside it, then re-resolves both components; nothing happens
         * when neither value changed. `eMCM_Undefined` is how a slot withdraws
         * its opinion and hands the component back to the slot below it.
         *
         * CryAction declares two same-name overloads and MSVC emits them
         * reversed, so this explicit-slot one comes first and the deprecated
         * two-argument wrapper -- which is only `(eMCMSlot_Game, h, v,
         * "SetMCM")` -- follows at slot 72.
         *
         * `reason` is kept by pointer and read back by the engine's own
         * diagnostics, so it must be a string literal.
         */
        virtual void SetMovementControlMethods(EMCMSlot slot, EMovementControlMethod horizontal, EMovementControlMethod vertical, const char *reason) = 0; // +0x238, slot 71
        virtual void unk_fn0072()                                                                                                                     = 0;
        virtual void unk_fn0073()                                                                                                                     = 0;
        virtual void unk_fn0074()                                                                                                                     = 0;
        /** The horizontal method the last resolve settled on. */
        virtual EMovementControlMethod GetMCMH() const = 0; // +0x258, slot 75
        /** The vertical method the last resolve settled on. */
        virtual EMovementControlMethod GetMCMV() const = 0; // +0x260, slot 76
        virtual void unk_fn0077()                      = 0;
        virtual void unk_fn0078()                      = 0;
        virtual void unk_fn0079()                      = 0;
        virtual void unk_fn0080()                      = 0;
        virtual void unk_fn0081()                      = 0;
        virtual void unk_fn0082()                      = 0;
        virtual void unk_fn0083()                      = 0;
        virtual void unk_fn0084()                      = 0;
        /** CryAction's IsLookIkAllowed: no look-IK block is held (+0x118 == 0, 0x18112E0EC). */
        virtual bool IsLookIkAllowed() const = 0; // slot 85, +0x2A8
        /**
         * CryAction's AllowAimIk: bit 0 of +0x11C (0x181AAE810).
         * C_ItemAttachmentManager::DrawBoundWeapon and CompleteItemTransfer
         * raise it as a weapon reaches the hand; StoreHandledWeapon drops it.
         */
        virtual void AllowAimIk(bool allow) = 0; // slot 86, +0x2B0
        /**
         * CryAction's IsAimIkAllowed (0x18154A9D0). The actor's aim-pose
         * update (0x180640950) gives a body that is not the local player an
         * aim pose only while this holds.
         */
        virtual bool IsAimIkAllowed() const = 0; // slot 87, +0x2B8
        virtual void unk_fn0088()                      = 0;
        virtual void unk_fn0089()                      = 0;
        /**
         * Suppresses animation-driven translation/rotation and clears acquired
         * movement before parameter calculation. Prediction overrides survive
         * but bypass smoothing; puppet motion enters SetDesiredLocalLocation.
         */
        virtual void SetNoMovementOverride(bool noMovement) = 0; // +0x2D0, slot 90
        virtual void unk_fn0091()                           = 0;
        virtual void unk_fn0092()                           = 0;
        virtual void unk_fn0093()                           = 0;
        /**
         * Adds a one-shot movement delta for this frame, which
         * `MergeEntityMovement` composes onto the merged movement as
         * `merged * forced` and `UpdatePhysicalEntityMovement` applies.
         *
         * Inert while `SetNoMovementOverride` is set: the merge answers
         * identity before it would have been composed on.
         */
        virtual void ForceMovement(const QuatT &movement) = 0; // +0x2F0, slot 94
        /**
         * States this body's world rotation for the frame, absolutely.
         *
         * `UpdatePhysicalEntityMovement` otherwise turns the entity by the
         * animation's root rotation every frame, off whatever it was last set
         * to; this replaces that result outright. One shot -- the same pass
         * clears it -- so it is stated every frame rather than on an edge.
         */
        virtual void ForceOverrideRotation(const Quat &rotation) = 0; // +0x2F8, slot 95
        virtual void unk_fn0096()                                = 0;
        virtual void unk_fn0097()                                = 0;
        virtual void unk_fn0098()                                = 0;
        virtual void unk_fn0099()                                = 0;
        virtual void unk_fn0100()                                = 0;
        virtual void unk_fn0101()                                = 0;
        virtual void unk_fn0102()                                = 0;
        virtual void unk_fn0103()                                = 0;
        virtual void unk_fn0104()                                = 0;
        // The flag word at CAnimatedCharacter+0x84E that UpdatePhysicalColliderMode
        // (0x18083C630) folds into the living entity's collision types: 0x8
        // drops rigid and sleeping-rigid bodies while
        // ac_disableLivingVsRigidCollisions is on. A player-ridden horse has it
        // removed (OnHorseRiderChanged 0x180A1C770).
        virtual void AddColliderFlags(std::uint16_t flags)       = 0; // +0x348, slot 105, 0x18044D980
        virtual void RemoveColliderFlags(std::uint16_t flags)    = 0; // +0x350, slot 106, 0x180A1C010
        virtual void unk_fn0107()                                = 0;
        virtual void unk_fn0108()                                = 0;
        virtual void unk_fn0109()                                = 0;
        virtual void unk_fn0110()                                = 0;
        virtual void unk_fn0111()                                = 0;
        virtual void unk_fn0112()                                = 0;
        virtual void unk_fn0113()                                = 0;
        virtual void unk_fn0114()                                = 0; // +0x390
    };

    static_assert(sizeof(IAnimatedCharacter) == 0x40, "IAnimatedCharacter size mismatch");
} // namespace SDK
