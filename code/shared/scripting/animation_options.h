#pragma once

namespace KCDC::Scripting {
    /**
     * The TypeScript shapes `playAnimation` takes its options in, on a player
     * and on an NPC alike.
     *
     * Kept here with the other option shapes: the contract generator reads
     * these strings verbatim into `server-api.d.ts`, so this is the one place
     * a shape is written down.
     *
     * A prop names either `item`, a row of `Animations.props()` whose model
     * and in-hand offset come with it, or `model`, any `objects/...cgf`. It
     * hangs on the right hand unless `hand` or `joint` (any skeleton joint)
     * says otherwise; `position` (metres) and `rotation` (a Quaternion or
     * Euler degrees) are offsets in the joint's own frame and replace the
     * item's own.
     */
    /** What `stopAnimation` takes: `standUp` plays the way out of a sitting or lying stance tag. */
    inline constexpr const char *kStopAnimationOptionsType = "{ standUp?: boolean }";

    /** Every name `Shared::Game::AnimationEndReasonName` gives, as the contract spells it. */
    inline constexpr const char *kAnimationEndReasonType = "'finished' | 'interrupted' | 'refused' | 'stopped' | 'replaced'";

    /** What `setFacialExpression` takes: `tags` pick the variant, the mood ones (`happy`, `angry`) among them. */
    inline constexpr const char *kFacialExpressionOptionsType = "{ tags?: string }";

    /** Every style `Shared::Game::TalkStyleName` gives but `none`, which is `setTalking(false)`. */
    inline constexpr const char *kTalkStyleType = "'neutral' | 'happy' | 'drunk' | 'chew'";

    inline constexpr const char *kAnimationOptionsType ="{ clip?: string; tags?: string; loop?: boolean; lockMovement?: boolean; props?: { item?: string; model?: string; hand?: 'right' | 'left'; joint?: string; position?: Vector3 | Partial<Vector3>; rotation?: Quaternion | Vector3 | Partial<Vector3> } | { item?: string; model?: string; hand?: 'right' | 'left'; joint?: string; position?: Vector3 | Partial<Vector3>; rotation?: Quaternion | Vector3 | Partial<Vector3> }[] }";
} // namespace KCDC::Scripting
