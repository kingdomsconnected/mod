#pragma once

namespace SDK::wh::dialogmodule {
    /**
     * One running conversation, as the rest of the game sees it: the
     * `I_Dialog` subobject of `C_DialogInstance`, at `+0x18`.
     *
     * Slots are proven against the implementation vtable `0x183E63B60`. Only
     * the ones the mod reads are named; the rest hold their place.
     */
    class I_Dialog {
      public:
        virtual void unk_fn00() = 0;
        virtual void unk_fn01() = 0;
        virtual void unk_fn02() = 0; // C_DialogInstance::Interrupt1804D075C
        virtual void unk_fn03() = 0;
        virtual void unk_fn04() = 0;
        virtual void unk_fn05() = 0;
        virtual void unk_fn06() = 0;
        virtual void unk_fn07() = 0; // C_DialogInstance::SetAIInteractionState1827FE5D8
        virtual void unk_fn08() = 0;
        virtual void unk_fn09() = 0;
        virtual void unk_fn10() = 0;
        virtual void unk_fn11() = 0;
        virtual void unk_fn12() = 0;
        virtual void unk_fn13() = 0;
        virtual void unk_fn14() = 0;
        virtual void unk_fn15() = 0;
        virtual void unk_fn16() = 0;
        virtual void unk_fn17() = 0;
        virtual void unk_fn18() = 0;
        virtual void unk_fn19() = 0; // C_DialogInstance::IsConfigFlag1Set1808DCB9C
        virtual void unk_fn20() = 0;
        virtual void unk_fn21() = 0; // C_DialogInstance::IsConfigFlag80000Set1803BD82C
        virtual void unk_fn22() = 0;

        /**
         * Exactly one participant (`0x1803BD810`). The game voices such an
         * instance through the `inner` standalone file purpose
         * (`C_DialogInstance::GetSpeakerFilePurpose1808DCB08`).
         */
        virtual bool HasSingleParticipant() const = 0; // 23
    };
} // namespace SDK::wh::dialogmodule
