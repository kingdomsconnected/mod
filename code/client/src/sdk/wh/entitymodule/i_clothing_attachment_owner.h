#pragma once

namespace SDK::wh::entitymodule {
    /**
     * Adapter interface used by clothing attachment managers to access either
     * an actor or an animation character.
     *
     * The two concrete implementations independently prove an exact 14-slot
     * table. The interface has no virtual destructor in this build.
     */
    class I_ClothingAttachmentOwner {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0; // final slot, +0x068

    };

    static_assert(sizeof(I_ClothingAttachmentOwner) == 0x8, "I_ClothingAttachmentOwner size mismatch");
} // namespace SDK::wh::entitymodule
