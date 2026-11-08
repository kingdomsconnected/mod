#include "ui_hud_notifications.h"

#include "features/items/custom_item_presentation.h"
#include "ui_localization.h"

#include "sdk/types.h"
#include "sdk/wh/framework/i_ui_hud_events.h"
#include "sdk/wh/guimodule/c_ui_hud_messages.h"

namespace KCDC::Features::UI {
    namespace {
        // Every slot is "call it if the HUD is there". The event dispatcher carries all of them but the info text, which has a receiver of its own; both come up and go away with the same HUD.
        template <typename Body>
        bool WithEvents(Body &&body) {
            auto *events = SDK::wh::framework::I_UIHudEvents::GetInstance();
            if (events == nullptr) {
                return false;
            }

            body(*events);

            return true;
        }

        template <typename Body>
        bool WithMessages(Body &&body) {
            auto *messages = SDK::wh::guimodule::C_UIHudMessages::GetInstance();
            if (messages == nullptr) {
                return false;
            }

            body(*messages);

            return true;
        }

        // Text under a key the engine's own resolver answers, since it '@'-prefixes every word of whatever it is handed.
        std::string AsKey(const std::string &text) {
            return KCDC::Features::UI::Localization::PublishText(text);
        }
    } // namespace

    bool Hud::ShowNotification(const std::string &message) {
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeShowGeneralNotificationRaw(SDK::ScopedCryString(AsKey(message).c_str())); });
    }

    bool Hud::ShowItemTransfer(const KCDC::Shared::RPC::ItemClassId &itemClass, int amount) {
        const auto native = KCDC::Features::Items::CustomItemPresentation::ToNative(itemClass);
        const SDK::CryGUID guid {native.m_low, native.m_high};
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeShowItemTransferRaw(guid, amount > 0 ? amount : -amount, amount > 0); });
    }

    bool Hud::ShowInfoText(const std::string &message, int durationMs, int priority) {
        return WithMessages([&](SDK::wh::guimodule::C_UIHudMessages &messages) { messages.InvokeShowInfoTextRaw(SDK::ScopedCryString(AsKey(message).c_str()), durationMs, priority); });
    }

    bool Hud::HideInfoText() {
        return WithMessages([](SDK::wh::guimodule::C_UIHudMessages &messages) { messages.InvokeHideInfoTextRaw(); });
    }

    bool Hud::ShowPerkUsed(const std::string &iconName, const std::string &name) {
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeShowPerkUsedRaw(SDK::ScopedCryString(iconName.c_str()), SDK::ScopedCryString(AsKey(name).c_str())); });
    }

    bool Hud::ShowPerkGained(const std::string &iconName, const std::string &name) {
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeShowPerkGainedRaw(SDK::ScopedCryString(iconName.c_str()), SDK::ScopedCryString(AsKey(name).c_str())); });
    }

    bool Hud::ShowComboLearned(const std::string &iconName, const std::string &name) {
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeShowComboLearnedRaw(SDK::ScopedCryString(iconName.c_str()), SDK::ScopedCryString(AsKey(name).c_str())); });
    }

    bool Hud::ShowReputationChanged(int eventId, const std::string &message) {
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeShowReputationChangedRaw(eventId, SDK::ScopedCryString(AsKey(message).c_str())); });
    }

    bool Hud::ShowRandomEventResult(int type, const std::string &result, const std::string &name) {
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeShowRandomEventResultRaw(type, SDK::ScopedCryString(AsKey(result).c_str()), SDK::ScopedCryString(AsKey(name).c_str())); });
    }

    bool Hud::ShowXpGain(const std::string &stat, const std::string &statName, int type) {
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeShowLevelXpGainRaw(SDK::ScopedCryString(stat.c_str()), SDK::ScopedCryString(AsKey(statName).c_str()), type); });
    }

    bool Hud::ShowTutorial(const std::string &tutorialName) {
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeShowTutorialRaw(SDK::ScopedCryString(tutorialName.c_str())); });
    }

    bool Hud::HideTutorial(const std::string &tutorialName) {
        return WithEvents([&](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeHideTutorialRaw(SDK::ScopedCryString(tutorialName.c_str())); });
    }

    bool Hud::HideCurrentTutorial() {
        return WithEvents([](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeHideCurrentTutorialRaw(); });
    }

    bool Hud::HideCodexActionHint() {
        return WithEvents([](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeHideCodexActionHintRaw(); });
    }

    bool Hud::ClearNotificationQueue() {
        return WithEvents([](SDK::wh::framework::I_UIHudEvents &events) { events.InvokeClearNotificationQueueRaw(); });
    }
} // namespace KCDC::Features::UI
