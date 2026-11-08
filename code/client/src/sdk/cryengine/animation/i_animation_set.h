#pragma once

#include <cstdint>

namespace SDK {
    /**
     * Abstract CryAnimation animation-set interface.
     *
     * RTTI identifies this global-namespace type as a struct. Its table at
     * 0x18406B9F0 contains exactly the virtual destructor and 39 pure
     * operations. Their stripped source contracts remain opaque.
     */
    struct IAnimationSet {
        virtual ~IAnimationSet() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual std::uint32_t GetAnimationCount() const = 0;
        virtual int GetAnimIDByName(const char *animationName) const = 0;
        virtual const char *GetNameByAnimID(int animationId) const = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        /**
         * Clip length in seconds for `animationId`, 0.0 for an id out of range.
         *
         * Slot 13 (+0x68). The implementation names itself: its bad-index
         * warning reads "illegal animation index used in function
         * GetDuration_sec'%d' for Model: %s". Reached the same way by both
         * CScriptBind_Entity::GetAnimationLength (0x1804D2960) and
         * CScriptBind_Entity::StartAnimation (0x1804D19B4), each going
         * GetIAnimationSet() -> GetAnimIDByName() -> here.
         *
         * For a parametric asset (a blend space) it averages the durations of
         * the sub-clips it recurses into, so the answer there is a mean rather
         * than an exact length. Single clips -- which is what the gate assets
         * are -- give the real length.
         */
        virtual float GetDuration_sec(int animationId) const = 0; // +0x68
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0; // +0x138
    };

    static_assert(sizeof(IAnimationSet) == 0x8,
                  "IAnimationSet size mismatch");
} // namespace SDK
