#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK::wh::dialogmodule {
    /**
     * The contract the interactive-dialogue HUD answers, and the one the game's
     * own dialogue system drives.
     *
     * Every slot below was identified from the Flash function the game's
     * implementation invokes, then confirmed against the element's declaration
     * in `Data/IPL_GameData.pak :: Libs/UI/UIElements/HUD.xml`, which is also
     * where the parameter names and units come from. Slots with no established
     * purpose stay opaque rather than being guessed at.
     *
     * The interface sits at `+0x58` inside `C_UIHudInteractiveDialog`, so a
     * pointer to one is the concrete object plus `sizeof(C_UIFlashBase)`.
     */
    class I_UIHudInteractiveDialog {
      public:
        virtual ~I_UIHudInteractiveDialog() = default; // 0

        /**
         * Appends one row to the pending list. Slot 1.
         *
         * `localize` is the interesting one: with it false the text reaches the
         * movie unmodified, so server-authored strings do not have to be
         * laundered through a localization key. `showMoney` is stored on the row
         * and gates the separate money readout rather than reaching Flash.
         *
         * `unknownFlag08` was called `enabled` here until a live test proved it
         * is not: every row passed `true` drew greyed and unpickable. What it
         * actually means is unestablished -- `C_Sequence` carries both `IsUsed`
         * and `GrayOutIfSequencesUsed`, so "already used" is the likely sense,
         * but that is a guess and the name no longer claims otherwise. Pass
         * `false` and set a row's real state through `SetEnableSequence`, which
         * is the movie's own declared way to say it.
         */
        virtual void AddDialogSequence(const SDK::CryStringT<char> &text, bool unknownFlag08, bool localize, const SDK::CryStringT<char> &secondary, int unknownA, const int &tenths, bool showMoney) = 0;

        /** Flushes the pending rows. `onRight` is which side the list draws on. Slot 2. */
        virtual void SetDialogSequences(bool onRight) = 0;

        /** Moves the highlight. `index` is zero based; `stopper` plays the can't-continue animation. Slot 3. */
        virtual void SetActiveSequence(int index, int stopper) = 0;

        /** Greys or restores one row. Slot 4. */
        virtual void SetEnableSequence(int index, bool enabled) = 0;

        virtual void SetDialogTimeLimit()                                                  = 0; // 5
        virtual void UpdateDialogTimeLimit(float unit)                                     = 0; // 6, a 1 -> 0 fraction, not a duration
        virtual void ClearDialogTimeLimit()                                                = 0; // 7
        virtual void Opaque08()                                                            = 0;
        virtual void ClearSkillChecks()                                                    = 0; // 9
        virtual void ShowSkillCheckResult(const SDK::CryStringT<char> &name, bool success) = 0; // 10

        virtual void Opaque11()            = 0; // SetDialogHaggle
        virtual void Opaque12()            = 0; // SetBarber
        virtual void ClearBarber()         = 0; // 13
        virtual void Opaque14()            = 0;
        virtual void Opaque15()            = 0; // SetShieldPainting
        virtual void ClearShieldPainting() = 0; // 16
        virtual void Opaque17()            = 0;
        virtual void HideMoney()           = 0; // 18
        virtual void Opaque19()            = 0;
        virtual void Opaque20()            = 0;
        virtual void Opaque21()            = 0;
    };
} // namespace SDK::wh::dialogmodule
