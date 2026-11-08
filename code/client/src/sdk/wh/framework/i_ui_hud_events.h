#pragma once

namespace SDK {
    template <typename Character> struct CryStringT;
    struct CryGUID;
} // namespace SDK

namespace SDK::wh::framework {
    /**
     * Native HUD event, notification, and tutorial presentation contract.
     *
     * Every slot is a one-shot Flash dispatch: it builds a command name,
     * marshals its arguments and hands them to the HUD element, so the command
     * each one pushes is what identifies it. Libs/UI/UIElements/HUD.xml declares
     * the Flash-side parameter names and types, and is the authority on them.
     * The queue lives on the ActionScript side, which is why
     * ClearNotificationQueue is the only state exposed over it.
     *
     * Every string argument is a localization key rather than text, and the
     * engine '@'-prefixes each of its words before resolving it.
     */
    class I_UIHudEvents {
      public:
        static I_UIHudEvents *GetInstance();

        /** CreateItems (0x182A1EA34) uses direction 0; DeleteItems' visitor (0x181EDFE90) uses 1. */
        void InvokeShowItemTransferRaw(const CryGUID &itemClass, int count, bool received) {
            ShowItemTransfer(&itemClass, count, received ? 0 : 1);
        }

        void InvokeShowGeneralNotificationRaw(const CryStringT<char> &message) {
            ShowGeneralNotification(message);
        }

        void InvokeShowPerkUsedRaw(const CryStringT<char> &iconName, const CryStringT<char> &uiName) {
            ShowPerkUsed(iconName, uiName);
        }

        void InvokeShowPerkGainedRaw(const CryStringT<char> &iconName, const CryStringT<char> &uiName) {
            ShowPerkGained(iconName, uiName);
        }

        void InvokeShowComboLearnedRaw(const CryStringT<char> &iconName, const CryStringT<char> &uiName) {
            ShowComboLearned(iconName, uiName);
        }

        void InvokeShowReputationChangedRaw(int eventId, const CryStringT<char> &message) {
            ShowReputationChanged(eventId, message);
        }

        void InvokeShowRandomEventResultRaw(int type, const CryStringT<char> &result, const CryStringT<char> &name) {
            ShowRandomEventResult(type, result, name);
        }

        void InvokeShowLevelXpGainRaw(const CryStringT<char> &stat, const CryStringT<char> &statName, int type) {
            ShowLevelXpGain(stat, statName, type);
        }

        void InvokeShowTutorialRaw(const CryStringT<char> &tutorialName) {
            ShowTutorial(tutorialName);
        }

        void InvokeHideTutorialRaw(const CryStringT<char> &tutorialName) {
            HideTutorial(tutorialName);
        }

        void InvokeHideCurrentTutorialRaw() {
            HideCurrentTutorial();
        }

        void InvokeShowOverlayTutorialRaw(const void *tutorialName) {
            Opaque20(tutorialName);
        }

        void InvokeHideCodexActionHintRaw() {
            HideCodexActionHint();
        }

        void InvokeClearNotificationQueueRaw() {
            ClearNotificationQueue();
        }

      private:

        // The item toasts. The leading argument is a 16-byte item identifier
        // the engine expands into the ClassId/UiName/IconId the Flash side
        // wants; its being a CryGUID is inferred from that use and its size.
        virtual void ShowItemTransfer(const CryGUID *itemClass, int count, unsigned char direction) = 0; // +0x00
        virtual void ShowItemCount(const CryGUID *itemClass, int count)                             = 0; // +0x08
        virtual void HideItemCount(const CryGUID *itemClass)                                        = 0; // +0x10

        // The plain notification: one line of text, no icon, no payload.
        virtual void ShowGeneralNotification(const CryStringT<char> &message) = 0; // +0x18

        // The leading `stat` is a string. The dispatcher copies it as a
        // CryStringT; passing an integer stat id faults during that copy.
        // Other vtable entries need their argument contracts checked too.
        virtual void ShowLevelUp(const CryStringT<char> &stat, const CryStringT<char> &statName, int type, int level, int perkPoints) = 0; // +0x20
        virtual void ShowLevelXpGain(const CryStringT<char> &stat, const CryStringT<char> &statName, int type)                        = 0; // +0x28

        // Icon-and-name toasts. The icon name is pushed raw; the ui name is a
        // localization key.
        virtual void ShowPerkUsed(const CryStringT<char> &iconName, const CryStringT<char> &uiName)     = 0; // +0x30
        virtual void ShowPerkGained(const CryStringT<char> &iconName, const CryStringT<char> &uiName)   = 0; // +0x38
        virtual void ShowComboLearned(const CryStringT<char> &iconName, const CryStringT<char> &uiName) = 0; // +0x40

        virtual void ShowReputationChanged(int eventId, const CryStringT<char> &message)                            = 0; // +0x48
        virtual void ShowRandomEventResult(int type, const CryStringT<char> &result, const CryStringT<char> &name)  = 0; // +0x50
        // The perk id is a CryGUID: the body memcmps it against the null guid
        // to decide whether to render it or substitute an empty string.
        virtual void ShowDiscoveredCodex(const CryStringT<char> &name, const CryStringT<char> &iconId, const CryGUID *perkId) = 0; // +0x58

        // The two "discovered" toasts. Each pushes its own Flash command and
        // nothing else: the map icon, the XP award and the discovery triggers
        // all hang off the status change instead, so silencing these two
        // removes the message without touching what discovery means.
        virtual void ShowDiscoveredPoi(int kind, const void *name, const void *a3, const void *guid) = 0;      // +0x60
        virtual void ShowDiscoveredLocation(int kind, const void *name, const void *a3, const void *guid) = 0; // +0x68

        virtual void HideCodexActionHint()                                                                   = 0; // +0x70
        virtual void ShowUnlockedShieldPaintingIngredient(unsigned char kind, const CryStringT<char> &name)   = 0; // +0x78

        virtual void ShowTutorial(const CryStringT<char> &tutorialName) = 0; // +0x80
        virtual void HideTutorial(const CryStringT<char> &tutorialName) = 0; // +0x88
        virtual void HideCurrentTutorial()                              = 0; // +0x90

        // Opaque19 and Opaque21 reach the same overlay-tutorial manager as
        // Opaque20 but take only flags, so what they switch is unproven.
        virtual void Opaque19(bool value)              = 0; // +0x98
        virtual void Opaque20(const void *tutorialName) = 0; // +0xA0
        virtual void Opaque21()                        = 0; // +0xA8

        virtual void ClearNotificationQueue() = 0; // +0xB0

        // No arguments, no strings, no reachable call site yet.
        virtual void Opaque23() = 0; // +0xB8
        virtual void Opaque24() = 0; // +0xC0
        virtual void Opaque25() = 0; // +0xC8
    };

    static_assert(sizeof(I_UIHudEvents) == 0x8,
                  "I_UIHudEvents size mismatch");
} // namespace SDK::wh::framework
