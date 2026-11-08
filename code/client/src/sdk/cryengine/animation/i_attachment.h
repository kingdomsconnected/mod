#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK {
    struct IAttachmentObject;
    struct IAttachmentSkin;
    struct ISkin;

    /** Attachment socket kinds accepted by IAttachmentManager::CreateAttachment. */
    enum EAttachmentType : std::uint32_t {
        CA_BONE = 0,
        CA_FACE = 1,
        CA_SKIN = 2,
        CA_PROX = 3,
        CA_PROW = 4,
        CA_VCLOTH = 5,
        CA_Invalid = 6,
    };

    /**
     * Abstract CryAnimation attachment interface.
     *
     * RTTI identifies this global-namespace type as a struct. Its table at
     * 0x183A65650 contains exactly 44 operations followed by the virtual
     * destructor. Thirty-three operations are pure and eleven have emitted
     * defaults. Core socket identity, transform, visibility, joint, and
     * binding operations are mapped below; project-specific additions remain
     * opaque.
     *
     * The slots a runtime prop socket uses are proven against the game's own
     * callers: `CScriptBind_Entity::SetAttachmentPos` reads slot 12 and writes
     * slot 11 (`SetAttRelativeDefault`, which `CAttachmentBONE` stores and
     * flags as authored rather than projected); `CProceduralClipAttachProp`
     * binds through slot 26 and reads slot 28. Slot 24 is
     * `CAttachmentBONE::AlignJointAttachment` (0x181069D30), which resolves
     * the joint the socket was created with, snaps the default to it and
     * leaves the relative offset at identity -- the step KCD2's
     * `CreateAttachment` never takes itself.
     */
    struct IAttachment {
        virtual void AddRef() = 0;
        virtual void Release() = 0;
        [[nodiscard]] virtual const char* GetName() const = 0;
        [[nodiscard]] virtual std::uint32_t GetNameCRC() const = 0;
        virtual std::uint32_t ReName(const char* socketName,
                                     std::uint32_t nameCrc) = 0;
        [[nodiscard]] virtual std::uint32_t GetType() const = 0;
        virtual std::uint32_t SetJointName(const char* jointName) = 0;
        virtual void SetFlags(std::uint32_t flags) = 0;
        [[nodiscard]] virtual std::uint32_t GetFlags() const = 0;
        virtual void SetAttAbsoluteDefault(const QuatT& transform) = 0;
        [[nodiscard]] virtual const QuatT& GetAttAbsoluteDefault() const = 0;
        virtual void SetAttRelativeDefault(const QuatT& transform) = 0;
        [[nodiscard]] virtual const QuatT& GetAttRelativeDefault() const = 0;
        [[nodiscard]] virtual const QuatT& GetAttModelRelative() const = 0;
        [[nodiscard]] virtual QuatTS GetAttWorldAbsolute() const = 0;
        [[nodiscard]] virtual const QuatT& GetAdditionalTransformation() const = 0;
        virtual void UpdateAttModelRelative() = 0;
        virtual void HideAttachment(std::uint32_t hide) = 0;
        [[nodiscard]] virtual std::uint32_t IsAttachmentHidden() const = 0;
        virtual void HideInRecursion(std::uint32_t hide) = 0;
        [[nodiscard]] virtual std::uint32_t IsAttachmentHiddenInRecursion() const = 0;
        virtual void HideInShadow(std::uint32_t hide) = 0;
        [[nodiscard]] virtual std::uint32_t IsAttachmentHiddenInShadow() const = 0;
        virtual void unk_fn0023() {} // emitted default
        virtual void AlignJointAttachment() = 0;
        [[nodiscard]] virtual std::uint32_t GetJointID() const = 0;
        virtual std::uint32_t AddBinding(IAttachmentObject* object,
                                         ISkin* skin = nullptr,
                                         std::uint32_t loadingFlags = 0) = 0;
        virtual void unk_fn0027() {} // emitted default
        [[nodiscard]] virtual IAttachmentObject* GetIAttachmentObject() const = 0;
        [[nodiscard]] virtual IAttachmentSkin* GetIAttachmentSkin() = 0;
        /**
         * Releases the bound payload and leaves the socket empty. Proven by
         * `CProceduralClipAttachProp::OnExit` (0x1836DF16C), which calls it
         * with 0 to take its prop back off; `CAttachmentBONE`'s entry
         * (0x180AB9C5C) ignores the argument and always releases.
         */
        virtual void ClearBinding(std::uint32_t loadingFlags = 0) = 0; // +0x0F0
        virtual std::uint32_t SwapBinding(IAttachment* newAttachment) = 0;
        virtual void unk_fn0032() {} // emitted default
        virtual void unk_fn0033() {} // emitted default
        virtual void unk_fn0034() {} // emitted default
        virtual void unk_fn0035() {} // emitted default
        virtual void unk_fn0036() {} // emitted default
        virtual void unk_fn0037() {} // emitted default
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() {} // emitted default
        virtual void unk_fn0042() {} // emitted default
        virtual void unk_fn0043() {} // emitted default
        virtual ~IAttachment() = default; // slot 44, +0x160
    };

    static_assert(sizeof(IAttachment) == 0x8,
                  "IAttachment size mismatch");
} // namespace SDK
