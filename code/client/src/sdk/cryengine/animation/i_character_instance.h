#pragma once

#include "../../types.h"
#include "../3dengine/s_rendering_pass_info.h"
#include "../renderer/s_rend_params.h"

#include <cstdint>

namespace SDK {
    struct IAttachmentManager;
    struct IAnimationSet;
    struct IDefaultSkeleton;
    struct ISkeletonAnim;
    struct ISkeletonPose;

    /**
     * Abstract CryAnimation character-instance interface.
     *
     * RTTI identifies this global-namespace type as a struct. Its table at
     * 0x1840600A8 contains exactly the virtual destructor and 72 pure
     * operations. KCD2's stripped slot contracts remain opaque.
     */
    /**
     * Stock `ECharRenderFlags` bits KCD2 keeps.
     *
     * `CS_FLAG_DRAW_MODEL` is the character's own draw gate:
     * `CCharInstance::Render` (0x1803D3384) returns before drawing anything
     * without it, and the attachment manager reads the same bit, so clothes
     * and gear go with the body. It is the way to hide a body -- clearing
     * `ENTITY_SLOT_RENDER` on its slot does not stop KCD2 drawing it.
     *
     * The character in an entity slot is animated only while the slot
     * renders, unless `CS_FLAG_UPDATE_ALWAYS` is set
     * in `CEntitySlot::GetBoundsChanged`.
     */
    inline constexpr int CS_FLAG_DRAW_MODEL    = 1 << 0;
    inline constexpr int CS_FLAG_UPDATE_ALWAYS = 1 << 3;

    struct ICharacterInstance {
        virtual ~ICharacterInstance()                                   = default;
        virtual void unk_fn0001()                                       = 0;
        virtual void unk_fn0002()                                       = 0;
        virtual void unk_fn0003()                                       = 0;
        virtual ISkeletonAnim *GetISkeletonAnim()                       = 0;
        virtual const ISkeletonAnim *GetISkeletonAnim() const           = 0;
        virtual ISkeletonPose *GetISkeletonPose()                       = 0;
        virtual const ISkeletonPose *GetISkeletonPose() const           = 0;
        virtual IAttachmentManager *GetIAttachmentManager()             = 0;
        virtual const IAttachmentManager *GetIAttachmentManager() const = 0;
        virtual IDefaultSkeleton &GetIDefaultSkeleton()                 = 0;
        virtual const IDefaultSkeleton &GetIDefaultSkeleton() const     = 0;
        virtual void unk_fn0012()                                       = 0;
        virtual void unk_fn0013()                                       = 0;
        virtual void unk_fn0014()                                       = 0;
        virtual IAnimationSet *GetIAnimationSet()                       = 0;
        virtual const IAnimationSet *GetIAnimationSet() const           = 0;
        virtual void unk_fn0017()                                       = 0;
        virtual void unk_fn0018()                                       = 0;
        virtual void unk_fn0019()                                       = 0;
        virtual void unk_fn0020()                                       = 0;
        virtual void unk_fn0021()                                       = 0;
        virtual void unk_fn0022()                                       = 0;
        virtual void unk_fn0023()                                       = 0;
        virtual void unk_fn0024()                                       = 0;
        /**
         * Submits the body and every attachment -- skin parts, clothes, hair,
         * head -- to a pass (`CCharInstance::Render` 0x1803D3384). KCD2 adds
         * the `QuatTS` offset 5.7 does not have; the entity slot render
         * (`CEntitySlotObject::Render` 0x18049F08C) passes identity. Draws
         * nothing unless `CS_FLAG_DRAW_MODEL` (bit 0 of the render flags) is
         * set, and stamps the frame it was drawn on, which keeps the
         * skeleton's update at full detail.
         */
        virtual void Render(const SRendParams &params, const QuatTS &offset, const SRenderingPassInfo &pass) = 0; // +0xC8
        /**
         * The character's `ECharRenderFlags` word, at `CCharInstance+0xA84`.
         * KCD2 carries four accessors where stock CryEngine has two: the
         * atomic add and clear sit between the stock setter and getter.
         * `CScriptBind_Entity::CharacterUpdateAlways` (0x1837D0E60) is the
         * caller that proves the add, with `CS_FLAG_UPDATE_ALWAYS`.
         */
        virtual void SetFlags(int flags)           = 0; // +0xD0, 0x180AE4350
        virtual void AddFlags(int flags)           = 0; // +0xD8, 0x1820EE840
        virtual void ClearFlags(int flags)         = 0; // +0xE0, 0x1820F0AF0
        virtual int GetFlags() const               = 0; // +0xE8, 0x1823CBA28
        virtual void unk_fn0030()                  = 0;
        virtual void unk_fn0031()                  = 0;
        virtual void unk_fn0032()                  = 0;
        virtual void unk_fn0033()                  = 0;
        virtual void unk_fn0034()                  = 0;
        virtual void unk_fn0035()                  = 0;
        virtual void unk_fn0036()                  = 0;
        virtual void unk_fn0037()                  = 0;
        virtual void unk_fn0038()                  = 0;
        virtual void unk_fn0039()                  = 0;
        virtual void unk_fn0040()                  = 0;
        virtual void SetPlaybackScale(float speed) = 0;
        virtual float GetPlaybackScale() const     = 0;
        /** Recent animation visibility; includes a grace period. */
        virtual std::uint32_t IsCharacterVisible() const = 0; // +0x158, 0x180642AF0
        virtual void unk_fn0044()                        = 0;
        virtual void unk_fn0045()                        = 0;
        virtual void unk_fn0046()                        = 0;
        virtual void unk_fn0047()                        = 0;
        virtual void unk_fn0048()                        = 0;
        virtual void unk_fn0049()                        = 0;
        virtual void unk_fn0050()                        = 0;
        virtual void unk_fn0051()                        = 0;
        virtual void unk_fn0052()                        = 0;
        virtual void unk_fn0053()                        = 0;
        virtual void unk_fn0054()                        = 0;
        virtual void unk_fn0055()                        = 0;
        virtual void unk_fn0056()                        = 0;
        virtual void unk_fn0057()                        = 0;
        virtual void unk_fn0058()                        = 0;
        virtual void unk_fn0059()                        = 0;
        virtual void unk_fn0060()                        = 0;
        virtual void unk_fn0061()                        = 0;
        virtual void unk_fn0062()                        = 0;
        virtual void unk_fn0063()                        = 0;
        virtual void unk_fn0064()                        = 0;
        virtual void unk_fn0065()                        = 0;
        virtual void unk_fn0066()                        = 0;
        virtual void unk_fn0067()                        = 0;
        virtual void unk_fn0068()                        = 0;
        virtual void unk_fn0069()                        = 0;
        virtual void unk_fn0070()                        = 0;
        virtual void unk_fn0071()                        = 0;
        virtual void unk_fn0072()                        = 0; // +0x240
    };

    static_assert(sizeof(ICharacterInstance) == 0x8, "ICharacterInstance size mismatch");
} // namespace SDK
