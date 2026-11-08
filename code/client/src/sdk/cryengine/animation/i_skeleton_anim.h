#pragma once

#include "../../types.h"
#include "anim_event_instance.h"
#include "cry_char_animation_params.h"

#include <cstdint>

namespace SDK {
    class CAnimation;
    struct ICharacterInstance;

    using CallBackFuncType = int (*)(ICharacterInstance *, void *);

    /**
     * Abstract CryAnimation skeleton-animation interface.
     *
     * RTTI identifies this global-namespace type as a struct. Its table at
     * 0x183A715C0 contains exactly the virtual destructor and 51 pure
     * operations. Their stripped source contracts remain opaque.
     */
    struct ISkeletonAnim {
        virtual ~ISkeletonAnim() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        /**
         * Enters or leaves the mode in which a caller poses the skeleton by
         * hand. Slot +0x28; the implementation writes bit 0 of `this+0x4BA`.
         *
         * Required, not optional, for hand-driven playback:
         * `UpdateAnimationTime` flags a `CA_MANUAL_UPDATE` animation for
         * removal from the queue once its transition weight reaches zero and
         * it is first in line -- unless this is set. Losing the clip out of
         * the FIFO then silently disables `SetLayerNormalizedTime`, which
         * only acts on a layer holding exactly one animation.
         */
        virtual void SetTrackViewExclusive(std::uint32_t exclusive) = 0; // +0x28

        /**
         * Blend weight between the two clips cross-fading in one layer.
         *
         * Slot +0x30. Refuses with "Trying to set manual mixing weight
         * without being in TrackViewExclusive mode" when the flag above is
         * clear, which is what pins both identities. Only meaningful with two
         * animations in the layer -- TrackView never touches it for a single
         * one, and neither need we.
         */
        virtual void SetTrackViewMixingWeight(std::uint32_t layer, float weight) = 0; // +0x30

        /** The flag SetTrackViewExclusive writes. Slot +0x38. */
        virtual std::uint32_t GetTrackViewStatus() const = 0; // +0x38
        virtual bool
        StartAnimation(const char *animationName,
                       const CryCharAnimationParams &params) = 0;
        virtual bool
        StartAnimationById(std::int32_t animationId,
                           const CryCharAnimationParams &params) = 0;
        virtual bool StopAnimationInLayer(std::int32_t layer,
                                          float blendOutTime) = 0;
        virtual bool StopAnimationsAllLayers() = 0;
        virtual CAnimation *FindAnimInFIFO(std::uint32_t userToken,
                                           int layer = 1) = 0;
        virtual const CAnimation *FindAnimInFIFO(std::uint32_t userToken,
                                                 int layer = 1) const = 0;
        virtual bool RemoveAnimFromFIFO(std::uint32_t layer,
                                        std::uint32_t index,
                                        bool forceRemove = false) = 0;
        virtual int GetNumAnimsInFIFO(std::uint32_t layer) const = 0;
        virtual void ClearFIFOLayer(std::uint32_t layer) = 0;
        virtual CAnimation &GetAnimFromFIFO(std::uint32_t layer,
                                            std::uint32_t index) = 0;
        virtual const CAnimation &GetAnimFromFIFO(
            std::uint32_t layer, std::uint32_t index) const = 0;
        /**
         * Fires the anim events crossed since this animation was last seeked,
         * and records `normalizedTime` as the new mark. Slot +0x98.
         *
         * This does not sample a pose. The virtual (0x180AB1340) is a thunk
         * into CSkeletonAnim::FireAnimEventsForSeek (0x180AB136C), which
         * resolves animation `index` in the layer and, only when
         * `normalizedTime` is greater than the animation's stored mark
         * (CAnimation +0x44) and `triggerAnimEvents` is set, dispatches the
         * events in between; it then stores `normalizedTime` at +0x44.
         * Seeking backwards fires nothing.
         *
         * TrackView calls this alongside SetLayerNormalizedTime every frame:
         * the other call moves the
         * pose, this one plays the sounds keyed along the clip. Driving a clip
         * by time without it is correct and completely silent.
         */
        virtual void ManualSeekAnimationInFIFO(std::uint32_t layer, std::uint32_t index, float normalizedTime, bool triggerAnimEvents) = 0; // +0x98
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        /**
         * The layer's intensity multiplier, over every clip it plays. Slot
         * +0xB0 (0x180AA8430): stores into the layer's transition queue
         * (`m_fLayerBlendWeight`, layer stride 0x40) and logs "invalid layer
         * id" past layer 15.
         *
         * Persistent: it outlives the clips and is 1.0 until someone writes
         * it. Mannequin rewrites it only for a scope with a playing action,
         * and `CLipSyncProvider_TransitionQueue` never writes it at all, so a
         * caller lowering it on the `LipSync` layer has to put 1.0 back.
         */
        virtual void SetLayerBlendWeight(std::int32_t layer, float weight) = 0; // +0xB0
        virtual void SetLayerPlaybackScale(std::int32_t layer, float speed) = 0;
        virtual float GetLayerPlaybackScale(std::uint32_t layer) const = 0;
        // CSkeletonAnim 0x18083C448 / 0x18083A7BC. Pickup's native writer
        // (0x180A7FFF4) supplies 0.0 for the unused frame-time argument.
        virtual void SetDesiredMotionParam(EMotionParamID id, float value, float frameTime = 0.0F) = 0;
        virtual bool GetDesiredMotionParam(EMotionParamID id, float &value) const                  = 0;
        virtual void SetAnimationNormalizedTime(CAnimation *animation,
                                                float normalizedTime,
                                                bool entireClip = true) = 0;
        virtual float
        GetAnimationNormalizedTime(const CAnimation *animation) const = 0;
        /**
         * Places the layer's clip at `normalizedTime` over the whole clip.
         *
         * Acts ONLY when that layer's FIFO holds exactly one animation: the
         * implementation (0x180AB1124) reads the count stored at the animation
         * array's pointer-4, returns untouched unless it is 1, and otherwise
         * forwards to SetAnimationNormalizedTime with entireClip set.
         *
         * So a caller driving a clip by time must start it on a cleared layer
         * with no transition. A blend-out from a previous clip leaves two
         * entries in the FIFO and silently disables seeking.
         *
         * This is what CScriptBind_Entity::SetAnimationTime calls, and through
         * it what AnimObject.lua uses to restore a part-played animation after
         * a quickload.
         */
        virtual void SetLayerNormalizedTime(std::uint32_t layer,
                                            float normalizedTime) = 0;
        virtual float GetLayerNormalizedTime(std::uint32_t layer) const = 0;
        virtual void unk_fn0031() = 0;
        virtual Vec3 GetCurrentVelocity() const = 0;
        virtual void SetEventCallback(CallBackFuncType callback,
                                      void *userData) = 0;
        virtual AnimEventInstance GetLastAnimEvent() = 0;

        /**
         * The movement the playing animations produced since the last update,
         * in character-local space -- root motion, before anything decides
         * whether the entity is allowed to follow it.
         *
         * Cached on the skeleton behind a validity flag and filled on first
         * ask each frame from CalculateRelativeMovement, so reading it is
         * free after the engine has already asked. `CAnimatedCharacter`
         * reads exactly this slot to build its animation-driven movement,
         * then rotates it by the entity's orientation; a caller wanting the
         * world delta does the same.
         *
         * Untouched by SetNoMovementOverride: that flag zeroes the animated
         * character's *use* of this, never the skeleton's own answer, so it
         * remains readable on a body whose transform is written from outside.
         */
        virtual const QuatT &GetRelMovement() const = 0; // +0x118

        virtual float GetUserData(int index) const = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        virtual void unk_fn0044() = 0;
        virtual void unk_fn0045() = 0;
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0; // +0x198
    };

    static_assert(sizeof(ISkeletonAnim) == 0x8,
                  "ISkeletonAnim size mismatch");
} // namespace SDK
