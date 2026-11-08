#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK {
    /** Global CryAction stance identifiers registered by KCD2. */
    enum EStance : std::int32_t {
        STANCE_NULL = -1,
        STANCE_STAND = 0,
        STANCE_CROUCH = 1,
        STANCE_PRONE = 2,
        STANCE_RELAXED = 3,
        STANCE_STEALTH = 4,
        STANCE_LOW_COVER = 5,
        STANCE_ALERTED = 6,
        STANCE_HIGH_COVER = 7,
        STANCE_SWIM = 8,
    };

    struct IAction;

    using TExactPositioningQueryID = std::uint32_t;

    /** Receives asynchronous exact-positioning lifecycle notifications. */
    struct IExactPositioningListener {
        virtual ~IExactPositioningListener() = default;
        virtual void ExactPositioningQueryComplete(
            TExactPositioningQueryID queryID, bool succeeded) = 0;
        virtual void ExactPositioningNotifyFinishPoint(const Vec3 &point) = 0;
    };

    static_assert(sizeof(IExactPositioningListener) == 0x8,
                  "IExactPositioningListener size mismatch");

    /**
     * Exact-positioning request used to enter synchronized actor actions.
     *
     * KCD2 retains CryAction's established layout but inserts an additional
     * query-ID output pointer at +0x48. The movement controller writes all
     * three query outputs, retains pAction, substitutes the configured default
     * when prepareRadius is negative, and consumes navSO while constructing
     * the exact-positioning target.
     *
     * CryStringT and pAction are native owning handles. Treat this structure as
     * a short-lived ABI record and keep referenced native objects alive until
     * RequestMovement has copied it.
     */
    struct SActorTargetParams {
        Vec3 location;                              // 0x00
        Vec3 direction;                             // 0x0C
        CryStringT<char> vehicleName;               // 0x18
        std::int32_t vehicleSeat;                   // 0x20
        float speed;                                // 0x24
        float directionTolerance;                   // 0x28, radians
        float startArcAngle;                        // 0x2C, radians
        float startWidth;                           // 0x30
        EStance stance;                             // 0x34
        TExactPositioningQueryID *pQueryStart;      // 0x38
        TExactPositioningQueryID *pQueryEnd;        // 0x40
        TExactPositioningQueryID *m_unknownQuery0048; // 0x48, KCD2 extension
        IAction *pAction;                           // 0x50, native _smart_ptr
        float prepareRadius;                        // 0x58
        bool projectEnd;                            // 0x5C
        bool navSO;                                 // 0x5D
        bool completeWhenActivated;                 // 0x5E
        std::byte m_padding005F;
    };

    static_assert(sizeof(SActorTargetParams) == 0x60,
                  "SActorTargetParams size mismatch");
    static_assert(offsetof(SActorTargetParams, pQueryStart) == 0x38,
                  "SActorTargetParams::pQueryStart offset mismatch");
    static_assert(offsetof(SActorTargetParams, m_unknownQuery0048) == 0x48,
                  "SActorTargetParams KCD2 query offset mismatch");
    static_assert(offsetof(SActorTargetParams, pAction) == 0x50,
                  "SActorTargetParams::pAction offset mismatch");
    static_assert(offsetof(SActorTargetParams, prepareRadius) == 0x58,
                  "SActorTargetParams::prepareRadius offset mismatch");

    /**
     * Read-side exact-positioning state exposed by IMovementController.
     *
     * This exact 0x58-byte record is created when an actor-target request is
     * accepted. It lets multiplayer code observe approach preparation, action
     * activation, AI-control handoff, the resolved transform and correction
     * velocities without reaching into the concrete controller.
     */
    struct SExactPositioningTarget {
        std::uint32_t preparing : 1;              // 0x00, bit 0
        std::uint32_t activated : 1;              // 0x00, bit 1
        mutable std::uint32_t notAiControlledAnymore : 1; // 0x00, bit 2
        std::uint32_t isNavigationalSO : 1;       // 0x00, bit 3
        std::uint32_t m_reservedStateBits : 28;
        QuatT location;                            // 0x04
        float startWidth;                          // 0x20
        float positionWidth;                       // 0x24
        float positionDepth;                       // 0x28
        float orientationTolerance;                // 0x2C
        Vec3 errorVelocity;                        // 0x30
        Quat errorRotationalVelocity;              // 0x3C
        IAction *pAction;                          // 0x50, native _smart_ptr
    };

    static_assert(sizeof(SExactPositioningTarget) == 0x58,
                  "SExactPositioningTarget size mismatch");
    static_assert(offsetof(SExactPositioningTarget, location) == 0x04,
                  "SExactPositioningTarget::location offset mismatch");
    static_assert(offsetof(SExactPositioningTarget, errorVelocity) == 0x30,
                  "SExactPositioningTarget::errorVelocity offset mismatch");
    static_assert(offsetof(SExactPositioningTarget, pAction) == 0x50,
                  "SExactPositioningTarget::pAction offset mismatch");

    /**
     * Writable movement command consumed by actor movement controllers.
     *
     * KCD2 retains CryAction's 64-bit presence/removal mask and the complete
     * classic field order through context at +0x13C, then extends the request
     * payload to 0x1B0 bytes. C_ActorMovementController::RequestMovement
     * independently corroborates every exposed offset and mask below. The
     * owning actor-target, prediction, mannequin and cover records remain
     * opaque until their complete KCD2 layouts are proven.
     *
     * This is an ABI overlay. Value-initialize a fresh request (`request{}`)
     * before setting fields so all unrequested tail state remains clear.
     */
    class CMovementRequest {
      public:
        bool IsEmpty() const { return m_flags == 0; }

        void SetLookTarget(const Vec3 &target, float importance = 1.0F) {
            m_lookTarget = target;
            m_lookImportance = importance;
            SetFlag(eMRF_LookTarget);
            ClearFlag(eMRF_RemoveLookTarget);
        }
        void ClearLookTarget() {
            ClearFlag(eMRF_LookTarget);
            SetFlag(eMRF_RemoveLookTarget);
        }
        bool HasLookTarget() const { return CheckFlag(eMRF_LookTarget); }
        bool RemoveLookTarget() const {
            return CheckFlag(eMRF_RemoveLookTarget);
        }
        const Vec3 &GetLookTarget() const { return m_lookTarget; }
        float GetLookImportance() const { return m_lookImportance; }

        /**
         * CryAction's look styles. A standing body whose look is Hard or Soft
         * also turns its lower body toward the look target: the movement
         * controller takes the look target as its body target ("lookIdleTurn",
         * C_ActorMovementController::Update 0x1804BA346). The NoLower styles do
         * not, nor does a controller holding no style or style 0, which it
         * reads as SoftNoLower (0x1804B9EAF).
         */
        enum ELookStyle : std::int32_t {
            LOOKSTYLE_DEFAULT      = 0,
            LOOKSTYLE_HARD         = 1,
            LOOKSTYLE_HARD_NOLOWER = 2,
            LOOKSTYLE_SOFT         = 3,
            LOOKSTYLE_SOFT_NOLOWER = 4,
        };
        bool HasLookStyle() const {
            return CheckFlag(eMRF_LookStyle);
        }
        ELookStyle GetLookStyle() const {
            return static_cast<ELookStyle>(m_lookStyle);
        }
        void SetLookStyle(ELookStyle style) {
            m_lookStyle = style;
            SetFlag(eMRF_LookStyle);
        }

        void SetAimTarget(const Vec3 &target) {
            m_aimTarget = target;
            SetFlag(eMRF_AimTarget);
            ClearFlag(eMRF_RemoveAimTarget);
        }
        void ClearAimTarget() {
            ClearFlag(eMRF_AimTarget);
            SetFlag(eMRF_RemoveAimTarget);
        }
        bool HasAimTarget() const { return CheckFlag(eMRF_AimTarget); }
        const Vec3 &GetAimTarget() const { return m_aimTarget; }

        void SetBodyTarget(const Vec3 &target) {
            m_bodyTarget = target;
            SetFlag(eMRF_BodyTarget);
            ClearFlag(eMRF_RemoveBodyTarget);
        }
        void ClearBodyTarget() {
            ClearFlag(eMRF_BodyTarget);
            SetFlag(eMRF_RemoveBodyTarget);
        }
        bool HasBodyTarget() const { return CheckFlag(eMRF_BodyTarget); }
        const Vec3 &GetBodyTarget() const { return m_bodyTarget; }

        void SetFireTarget(const Vec3 &target) {
            m_fireTarget = target;
            SetFlag(eMRF_FireTarget);
            ClearFlag(eMRF_RemoveFireTarget);
        }
        void ClearFireTarget() {
            ClearFlag(eMRF_FireTarget);
            SetFlag(eMRF_RemoveFireTarget);
        }
        bool HasFireTarget() const { return CheckFlag(eMRF_FireTarget); }
        const Vec3 &GetFireTarget() const { return m_fireTarget; }

        void SetDesiredSpeed(float speed, float targetSpeed = -1.0F) {
            m_desiredSpeed = speed;
            m_targetSpeed = targetSpeed >= 0.0F ? targetSpeed : speed;
            SetFlag(eMRF_DesiredSpeed);
            ClearFlag(eMRF_RemoveDesiredSpeed);
        }
        /**
         * The pace, as an `E_MovementSpeed` ordinal -- idle 0, walk 1, run 2,
         * sprint 3, dash 4 -- and not a speed.
         *
         * This is the only source of the MoveSpeed animation tag.
         * `C_ActorMovementController::Update` (0x1804B8E88) reads it from
         * `m_state.pseudoSpeed` at `+0x118`, floors it at 1.0 so a moving body
         * is never below walk, and writes it onto the actor's AI animation
         * state; `C_Actor::UpdateMannequinTags` reads it back through vslot 134
         * (0x180645870) and `ResolveMoveSpeedTag` takes the tier as
         * `round(it) - 1`.
         *
         * `SetDesiredSpeed` is a different field and a real speed: it moves the
         * body and names no tag, so a request carrying only that one travels
         * without a walk cycle.
         */
        void SetPseudoSpeed(float pseudoSpeed) {
            m_pseudoSpeed = pseudoSpeed;
            SetFlag(eMRF_PseudoSpeed);
        }
        bool HasPseudoSpeed() const {
            return CheckFlag(eMRF_PseudoSpeed);
        }
        float GetPseudoSpeed() const { return m_pseudoSpeed; }

        void ClearDesiredSpeed() {
            ClearFlag(eMRF_DesiredSpeed);
            SetFlag(eMRF_RemoveDesiredSpeed);
        }
        bool HasDesiredSpeed() const { return CheckFlag(eMRF_DesiredSpeed); }
        float GetDesiredSpeed() const { return m_desiredSpeed; }
        float GetDesiredTargetSpeed() const { return m_targetSpeed; }

        void SetDistanceToPathEnd(float distance) {
            m_distanceToPathEnd = distance;
        }
        void ClearDistanceToPathEnd() { m_distanceToPathEnd = -1.0F; }
        float GetDistanceToPathEnd() const { return m_distanceToPathEnd; }

        void SetDirectionOffFromPath(const Vec3 &direction) {
            m_dirOffFromPath = direction;
            SetFlag(eMRF_DirectionOffPath);
        }
        void ClearDirectionOffFromPath() {
            m_dirOffFromPath = {};
            ClearFlag(eMRF_DirectionOffPath);
        }
        bool HasDirectionOffFromPath() const {
            return CheckFlag(eMRF_DirectionOffPath);
        }
        const Vec3 &GetDirOffFromPath() const {
            return m_dirOffFromPath;
        }

        void SetStance(EStance stance) {
            m_stance = stance;
            SetFlag(eMRF_Stance);
            ClearFlag(eMRF_RemoveStance);
        }
        void ClearStance() {
            ClearFlag(eMRF_Stance);
            SetFlag(eMRF_RemoveStance);
        }
        bool HasStance() const { return CheckFlag(eMRF_Stance); }
        EStance GetStance() const { return m_stance; }

        void SetMoveTarget(const Vec3 &target) {
            m_moveTarget = target;
            SetFlag(eMRF_MoveTarget);
            ClearFlag(eMRF_RemoveMoveTarget);
        }
        void ClearMoveTarget() {
            ClearFlag(eMRF_MoveTarget);
            SetFlag(eMRF_RemoveMoveTarget);
        }
        bool HasMoveTarget() const { return CheckFlag(eMRF_MoveTarget); }
        const Vec3 &GetMoveTarget() const { return m_moveTarget; }

        void SetInflectionPoint(const Vec3 &point) {
            m_inflectionPoint = point;
            SetFlag(eMRF_InflectionPoint);
            ClearFlag(eMRF_RemoveInflectionPoint);
        }
        void ClearInflectionPoint() {
            ClearFlag(eMRF_InflectionPoint);
            SetFlag(eMRF_RemoveInflectionPoint);
        }
        bool HasInflectionPoint() const {
            return CheckFlag(eMRF_InflectionPoint);
        }
        const Vec3 &GetInflectionPoint() const { return m_inflectionPoint; }

        void SetDesiredBodyDirectionAtTarget(const Vec3 &direction) {
            m_desiredBodyDirectionAtTarget = direction;
            SetFlag(eMRF_DesiredBodyDirectionAtTarget);
            ClearFlag(eMRF_RemoveDesiredBodyDirectionAtTarget);
        }
        void ClearDesiredBodyDirectionAtTarget() {
            ClearFlag(eMRF_DesiredBodyDirectionAtTarget);
            SetFlag(eMRF_RemoveDesiredBodyDirectionAtTarget);
        }
        bool HasDesiredBodyDirectionAtTarget() const {
            return CheckFlag(eMRF_DesiredBodyDirectionAtTarget);
        }
        const Vec3 &GetDesiredBodyDirectionAtTarget() const {
            return m_desiredBodyDirectionAtTarget;
        }

        void AddDeltaMovement(const Vec3 &delta) {
            if (CheckFlag(eMRF_DeltaMovement)) {
                m_deltaMovement.x += delta.x;
                m_deltaMovement.y += delta.y;
                m_deltaMovement.z += delta.z;
            } else {
                m_deltaMovement = delta;
            }
            SetFlag(eMRF_DeltaMovement);
        }
        void RemoveDeltaMovement() { ClearFlag(eMRF_DeltaMovement); }
        bool HasDeltaMovement() const { return CheckFlag(eMRF_DeltaMovement); }
        const Vec3 &GetDeltaMovement() const { return m_deltaMovement; }

        void AddDeltaRotation(const Ang3 &delta) {
            if (CheckFlag(eMRF_DeltaRotation)) {
                m_deltaRotation.x += delta.x;
                m_deltaRotation.y += delta.y;
                m_deltaRotation.z += delta.z;
            } else {
                m_deltaRotation = delta;
            }
            SetFlag(eMRF_DeltaRotation);
        }
        void RemoveDeltaRotation() { ClearFlag(eMRF_DeltaRotation); }
        bool HasDeltaRotation() const { return CheckFlag(eMRF_DeltaRotation); }
        const Ang3 &GetDeltaRotation() const { return m_deltaRotation; }

        /**
         * CryAction's body orientation mode, merged by RequestMovement
         * (0x180837E7C) under bit 24. With an aim target, strafing allowed and
         * the body moving, C_ActorMovementController::Update (0x1804B8E88)
         * faces it at the aim for 1 ("aimStrafe") and halfway between the aim
         * and the travel for 2 ("aimStrafeHalf"); any other value leaves the
         * body target alone. A standing body turns to the aim whatever it is
         * ("aimIdleTurn").
         */
        std::int32_t GetBodyOrientationMode() const { return m_bodyOrientationMode; }
        void SetBodyOrientationMode(std::int32_t mode) {
            m_bodyOrientationMode = mode;
            SetFlag(eMRF_BodyOrientationMode);
        }

        void SetLean(float lean) {
            m_desiredLean = lean;
            SetFlag(eMRF_DesiredLean);
            ClearFlag(eMRF_RemoveDesiredLean);
        }
        void ClearLean() {
            ClearFlag(eMRF_DesiredLean);
            SetFlag(eMRF_RemoveDesiredLean);
        }
        bool HasLean() const { return CheckFlag(eMRF_DesiredLean); }
        float GetLean() const { return m_desiredLean; }

        void SetPeekOver(float peekOver) {
            m_desiredPeekOver = peekOver;
            SetFlag(eMRF_DesiredPeekOver);
            ClearFlag(eMRF_RemoveDesiredPeekOver);
        }
        void ClearPeekOver() {
            ClearFlag(eMRF_DesiredPeekOver);
            SetFlag(eMRF_RemoveDesiredPeekOver);
        }
        bool HasPeekOver() const { return CheckFlag(eMRF_DesiredPeekOver); }
        float GetPeekOver() const { return m_desiredPeekOver; }

        void SetForcedNavigation(const Vec3 &navigation) {
            m_forcedNavigation = navigation;
            SetFlag(eMRF_ForcedNavigation);
            ClearFlag(eMRF_RemoveForcedNavigation);
        }
        void ClearForcedNavigation() {
            m_forcedNavigation = {};
            ClearFlag(eMRF_ForcedNavigation);
            SetFlag(eMRF_RemoveForcedNavigation);
        }
        bool HasForcedNavigation() const {
            return CheckFlag(eMRF_ForcedNavigation);
        }
        const Vec3 &GetForcedNavigation() const { return m_forcedNavigation; }

        void SetContext(std::uint32_t context) {
            m_context = context;
            SetFlag(eMRF_Context);
            ClearFlag(eMRF_RemoveContext);
        }
        void ClearContext() {
            ClearFlag(eMRF_Context);
            SetFlag(eMRF_RemoveContext);
        }
        bool HasContext() const { return CheckFlag(eMRF_Context); }
        std::uint32_t GetContext() const { return m_context; }

        void SetJump() { SetFlag(eMRF_Jump); }
        void ClearJump() { ClearFlag(eMRF_Jump); }
        bool ShouldJump() const { return CheckFlag(eMRF_Jump); }

        void SetNoAiming() { SetFlag(eMRF_NoAiming); }
        bool HasNoAiming() const { return CheckFlag(eMRF_NoAiming); }

        void SetAllowStrafing() { SetFlag(eMRF_AllowStrafing); }
        void SetAllowStrafing(bool allow) {
            allow ? SetFlag(eMRF_AllowStrafing) : ClearFlag(eMRF_AllowStrafing);
        }
        void ClearAllowStrafing() { ClearFlag(eMRF_AllowStrafing); }
        bool AllowStrafing() const { return CheckFlag(eMRF_AllowStrafing); }

        /**
         * Lets the body turn to the body target. Without it the controller's
         * Update (0x1804B9B03) takes the "lockedturn" branch and holds the
         * body's current facing, whatever body target it holds. Sticky in the
         * controller until removed.
         */
        void SetAllowLowerBodyToTurn() {
            SetFlag(eMRF_AllowLowerBodyToTurn);
            ClearFlag(eMRF_RemoveAllowLowerBodyToTurn);
        }
        /** Takes the permission back out of the controller (bit 54, merged in RequestMovement). */
        void RemoveAllowLowerBodyToTurn() {
            ClearFlag(eMRF_AllowLowerBodyToTurn);
            SetFlag(eMRF_RemoveAllowLowerBodyToTurn);
        }
        void SetAllowLowerBodyToTurn(bool allow) {
            allow ? SetFlag(eMRF_AllowLowerBodyToTurn)
                  : ClearFlag(eMRF_AllowLowerBodyToTurn);
        }
        void ClearLowerBodyToTurn() {
            ClearFlag(eMRF_AllowLowerBodyToTurn);
        }
        bool AllowLowerBodyToTurn() const {
            return CheckFlag(eMRF_AllowLowerBodyToTurn);
        }

        /**
         * Drops the controller's motion prediction (bit 44, merged in
         * RequestMovement 0x180837E7C as "clear bit 13 on the state").
         *
         * `C_ActorMovementController::Update` writes a standing body's turn
         * angle -- the `eMotionParamID_TurnAngle` that makes
         * `CAnimActionAIMovement::SelectState` pick a turn-in-place fragment --
         * only while the merged request holds no prediction (0x1804BBFA3).
         * The controller clears it itself for a player-controlled actor
         * at 0x1804BBC36. Other bodies keep their first prediction until a
         * request clears it.
         */
        void ClearPrediction() {
            ClearFlag(eMRF_Prediction);
            SetFlag(eMRF_RemovePrediction);
        }
        bool HasPrediction() const { return CheckFlag(eMRF_Prediction); }
        bool RemovesPrediction() const { return CheckFlag(eMRF_RemovePrediction); }

        void SetActorTarget(const SActorTargetParams &params) {
            m_actorTarget = params;
            SetFlag(eMRF_ActorTarget);
            ClearFlag(eMRF_RemoveActorTarget);
        }
        void ClearActorTarget() {
            ClearFlag(eMRF_ActorTarget);
            SetFlag(eMRF_RemoveActorTarget);
        }
        bool HasActorTarget() const { return CheckFlag(eMRF_ActorTarget); }
        bool RemoveActorTarget() const {
            return CheckFlag(eMRF_RemoveActorTarget);
        }
        const SActorTargetParams &GetActorTarget() const {
            return m_actorTarget;
        }

      public:
        /**
         * Which of the request's fields carry something this frame, and which
         * of them the movement controller is being asked to forget.
         *
         * The native `eMRF_` flags occupy a 64-bit word. This enum keeps
         * those bit values with an explicit underlying type.
         *
         * A remove bit is its parameter's bit shifted up by 31, which is why
         * the two halves are written that way rather than as two lists.
         */
        enum EMovementRequestFlags : std::uint64_t {
            eMRF_LookTarget                  = 1ULL << 0,
            eMRF_AimTarget                   = 1ULL << 1,
            eMRF_DesiredSpeed                = 1ULL << 2,
            eMRF_Stance                      = 1ULL << 3,
            eMRF_MoveTarget                  = 1ULL << 4,
            eMRF_DeltaMovement               = 1ULL << 5,
            eMRF_DeltaRotation               = 1ULL << 6,
            // Bit 7, proven in RequestMovement (0x1808380D1): the look style at
            // +0x138 is copied when it is set.
            eMRF_LookStyle                   = 1ULL << 7,
            // Bit 12, proven in C_ActorMovementController::RequestMovement
            // (0x180837E7C): the pseudo-speed branch copies the float at +0x108
            // and ORs 0x1000 into the flags.
            eMRF_PseudoSpeed                 = 1ULL << 12,
            // Bit 13, proven in RequestMovement (0x180837E7C): the prediction
            // at +0x10C is copied when it is set (CMovementRequest::HasPrediction,
            // 0x1823CE164).
            eMRF_Prediction                  = 1ULL << 13,
            eMRF_DesiredLean                 = 1ULL << 8,
            eMRF_DesiredPeekOver             = 1ULL << 9,
            eMRF_ActorTarget                 = 1ULL << 10,
            eMRF_FireTarget                  = 1ULL << 11,
            eMRF_ForcedNavigation            = 1ULL << 14,
            eMRF_BodyTarget                  = 1ULL << 15,
            eMRF_InflectionPoint             = 1ULL << 16,
            eMRF_Context                     = 1ULL << 17,
            eMRF_DesiredBodyDirectionAtTarget = 1ULL << 18,

            eMRF_Jump                        = 1ULL << 19,
            eMRF_NoAiming                    = 1ULL << 20,
            eMRF_AllowStrafing               = 1ULL << 21,
            eMRF_DirectionOffPath            = 1ULL << 22,
            eMRF_AllowLowerBodyToTurn        = 1ULL << 23,
            // Bit 24, proven in RequestMovement (0x180837E7C): copies the
            // int at +0x1A8 (sub_1823CE110 tests it).
            eMRF_BodyOrientationMode         = 1ULL << 24,

            eMRF_RemoveLookTarget            = eMRF_LookTarget << 31,
            eMRF_RemoveAimTarget             = eMRF_AimTarget << 31,
            eMRF_RemoveDesiredSpeed          = eMRF_DesiredSpeed << 31,
            eMRF_RemoveStance                = eMRF_Stance << 31,
            eMRF_RemoveMoveTarget            = eMRF_MoveTarget << 31,
            eMRF_RemoveDesiredLean           = eMRF_DesiredLean << 31,
            eMRF_RemoveDesiredPeekOver       = eMRF_DesiredPeekOver << 31,
            eMRF_RemoveActorTarget           = eMRF_ActorTarget << 31,
            eMRF_RemoveFireTarget            = eMRF_FireTarget << 31,
            eMRF_RemoveForcedNavigation      = eMRF_ForcedNavigation << 31,
            eMRF_RemoveBodyTarget            = eMRF_BodyTarget << 31,
            eMRF_RemoveInflectionPoint       = eMRF_InflectionPoint << 31,
            eMRF_RemoveContext               = eMRF_Context << 31,
            eMRF_RemoveDesiredBodyDirectionAtTarget = eMRF_DesiredBodyDirectionAtTarget << 31,
            eMRF_RemoveAllowLowerBodyToTurn  = eMRF_AllowLowerBodyToTurn << 31,
            eMRF_RemovePrediction            = eMRF_Prediction << 31, // CMovementRequest::RemovePrediction, 0x1823CE1D8
        };

      private:
        void ClearFlag(EMovementRequestFlags flag) { m_flags &= ~static_cast<std::uint64_t>(flag); }
        void SetFlag(EMovementRequestFlags flag) { m_flags |= static_cast<std::uint64_t>(flag); }
        bool CheckFlag(EMovementRequestFlags flag) const { return (m_flags & static_cast<std::uint64_t>(flag)) != 0; }

        std::uint64_t m_flags;                     // 0x000
        Vec3 m_lookTarget;                         // 0x008
        Vec3 m_aimTarget;                          // 0x014
        Vec3 m_bodyTarget;                         // 0x020
        Vec3 m_fireTarget;                         // 0x02C
        float m_desiredSpeed;                      // 0x038
        float m_targetSpeed;                       // 0x03C
        float m_lookImportance;                    // 0x040
        float m_distanceToPathEnd;                 // 0x044
        Vec3 m_dirOffFromPath;                     // 0x048
        EStance m_stance;                          // 0x054
        Vec3 m_moveTarget;                         // 0x058
        Vec3 m_inflectionPoint;                    // 0x064
        Vec3 m_desiredBodyDirectionAtTarget;       // 0x070
        Vec3 m_deltaMovement;                      // 0x07C
        Ang3 m_deltaRotation;                      // 0x088
        float m_desiredLean;                       // 0x094
        float m_desiredPeekOver;                   // 0x098
        std::byte m_padding009C[0x4];
        SActorTargetParams m_actorTarget;           // 0x0A0
        std::int32_t m_alertness;                  // 0x100
        std::int32_t m_alertnessLast;              // 0x104
        float m_pseudoSpeed;                       // 0x108
        std::byte m_prediction[0x20];               // 0x10C
        Vec3 m_forcedNavigation;                   // 0x12C
        std::int32_t m_lookStyle;                  // 0x138
        std::uint32_t m_context;                   // 0x13C
        std::byte m_extendedRequestState[0x68];     // 0x140
        std::int32_t m_bodyOrientationMode;        // 0x1A8
        std::uint32_t m_padding01AC;
    };

    static_assert(sizeof(CMovementRequest) == 0x1B0,
                  "CMovementRequest size mismatch");

    /**
     * World-space stance snapshot returned by KCD2 movement controllers.
     *
     * The KCD2 constructor preserves CryAction's 0xA4-byte field order. The
     * concrete actor update independently writes the entity transform, body,
     * weapon, aim, fire, eye and bounds fields at these exact offsets.
     */
    struct SStanceState {
        Vec3 pos;                    // 0x00
        Vec3 entityDirection;        // 0x0C
        Vec3 animationBodyDirection; // 0x18
        Vec3 upDirection;            // 0x24
        Vec3 weaponPosition;         // 0x30
        Vec3 aimDirection;           // 0x3C
        Vec3 fireDirection;          // 0x48
        Vec3 eyePosition;            // 0x54
        Vec3 eyeDirection;           // 0x60
        float lean;                  // 0x6C
        float peekOver;              // 0x70
        AABB m_StanceSize;           // 0x74
        AABB m_ColliderSize;         // 0x8C
    };

    /**
     * Exact 0xF4-byte movement snapshot shared by players, NPCs and animals.
     *
     * KCD2 retains the CryAction SStanceState prefix and SMovementState tail.
     * Its constructor establishes the stance sentinel, speed defaults and
     * liveness flag, while C_ActorMovementController's synchronized update
     * writes the same fields before GetMovementState copies the snapshot.
     */
    struct SMovementState : SStanceState {
        Vec3 fireTarget;                   // 0xA4
        EStance stance;                    // 0xB0
        Vec3 animationEyeDirection;        // 0xB4
        Vec3 movementDirection;            // 0xC0
        Vec3 lastMovementDirection;        // 0xCC
        float desiredSpeed;                // 0xD8
        float minSpeed;                    // 0xDC
        float normalSpeed;                 // 0xE0
        float maxSpeed;                    // 0xE4
        float slopeAngle;                  // 0xE8
        bool atMoveTarget;                 // 0xEC
        bool isAlive;                      // 0xED
        bool isAiming;                     // 0xEE
        bool isFiring;                     // 0xEF
        bool isVisible;                    // 0xF0
        bool isMoving;                     // 0xF1
        std::byte m_padding00F2[0x2];
    };

    /** Input contract for IMovementController::GetStanceState. */
    struct SStanceStateQuery {
        EStance stance;            // 0x00
        float lean;                // 0x04
        float peekOver;            // 0x08
        bool defaultPose;          // 0x0C
        std::byte m_padding000D[0x3];
        Vec3 position;             // 0x10
        Vec3 target;               // 0x1C
    };

    /**
     * IMovementController as implemented by
     * wh::entitymodule::C_ActorMovementController.
     *
     * The complete 17-slot concrete table is at 0x183E74AC0. KCD2 retains
     * the six established operations after the destructor and appends ten
     * unidentified entries. GetMovementState adds a KCD2 boolean update
     * argument, proven by the concrete implementation's state-refresh path.
     */
    class IMovementController {
      public:
        virtual ~IMovementController() = default;
        virtual bool RequestMovement(CMovementRequest &request) = 0;
        virtual void GetMovementState(SMovementState &state, bool updateState) = 0;
        virtual bool GetStanceState(const SStanceStateQuery &query, SStanceState &state) = 0;
        virtual Vec2 GetDesiredMoveDir() const = 0;
        virtual void SetExactPositioningListener(IExactPositioningListener *listener) = 0;
        virtual const SExactPositioningTarget *GetExactPositioningTarget() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0; // +0x080
    };

    static_assert(sizeof(IMovementController) == 0x8, "IMovementController size mismatch");
    static_assert(sizeof(SStanceState) == 0xA4,
                  "SStanceState size mismatch");
    static_assert(offsetof(SStanceState, weaponPosition) == 0x30,
                  "SStanceState::weaponPosition offset mismatch");
    static_assert(offsetof(SStanceState, eyePosition) == 0x54,
                  "SStanceState::eyePosition offset mismatch");
    static_assert(offsetof(SStanceState, m_StanceSize) == 0x74,
                  "SStanceState::m_StanceSize offset mismatch");
    static_assert(sizeof(SMovementState) == 0xF4,
                  "SMovementState size mismatch");
    static_assert(sizeof(SStanceStateQuery) == 0x28,
                  "SStanceStateQuery size mismatch");
    static_assert(offsetof(SStanceStateQuery, position) == 0x10,
                  "SStanceStateQuery::position offset mismatch");
    static_assert(offsetof(SStanceStateQuery, target) == 0x1C,
                  "SStanceStateQuery::target offset mismatch");
} // namespace SDK
