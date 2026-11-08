#include <utils/safe_win32.h>

#include "features/dialogue/dialogue_module.h"

#include "features/ui/native_element.h"

#include "sdk/cryengine/scaleform/s_flash_display_info.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"

#include "shared/features/dialogue/dialogue_rpc.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>

#include <algorithm>

namespace KCDC::Features::Dialogue {
    namespace {
        /**
         * Our own map rather than the game's `dialog` one.
         *
         * The game's map is bound to its own dialogue controller, which owns a
         * selection index of its own; borrowing it would put two owners on one
         * highlight. `exclusivity="1"` is what stops movement and look reaching
         * the player while a page is up, and it is the same shape the native UI
         * focus map already uses.
         */
        constexpr char kMapName[] = "kcdc_dialogue";
        /**
         * The keys are the game's own, read out of its keybind table rather than
         * picked: `Libs/Config/keybindSuperactions.xml` files `dialog_confirm`
         * under the `primary_action` superaction, whose keyboard controls are
         * `e` and `enter`, and both are bound. `space` is not confirm at all --
         * the game files it under `skip_dialog`, a different action this list
         * does not have.
         *
         * The mouse follows the game's `dialog` map too: the wheel is its
         * `scroll_up`/`scroll_down`, moving the highlight a row like the arrows,
         * and the button is `dialog_confirm_mouse`. The movie reports no clicked
         * row back -- the highlight is owned here and only pushed out -- so a
         * click confirms the highlighted row, exactly as it does natively.
         *
         * Every input goes through the `<keyboard>`/`<xboxpad>` child form. The
         * attribute form takes one input per device, and mixing the two on one
         * action asks the same key to be bound twice.
         *
         * `priority="menu"` outranks the `minigames` band the game's own `dialog`
         * map sits in, so an exclusive map here wins the keys it names.
         */
        constexpr char kMapXml[] = "<actionmap name=\"kcdc_dialogue\" priority=\"menu\" exclusivity=\"1\">"
                                   "<action name=\"kcdc_dlg_up\" onPress=\"1\" onHold=\"1\" holdTriggerDelay=\"0.4\" holdRepeatDelay=\"0.15\">"
                                   "<keyboard><inputdata input=\"up\"/><inputdata input=\"w\"/><inputdata input=\"mwheel_up\"/></keyboard>"
                                   "<xboxpad><inputdata input=\"xi_dpad_up\"/><inputdata input=\"xi_thumbl_up\"/></xboxpad>"
                                   "</action>"
                                   "<action name=\"kcdc_dlg_down\" onPress=\"1\" onHold=\"1\" holdTriggerDelay=\"0.4\" holdRepeatDelay=\"0.15\">"
                                   "<keyboard><inputdata input=\"down\"/><inputdata input=\"s\"/><inputdata input=\"mwheel_down\"/></keyboard>"
                                   "<xboxpad><inputdata input=\"xi_dpad_down\"/><inputdata input=\"xi_thumbl_down\"/></xboxpad>"
                                   "</action>"
                                   "<action name=\"kcdc_dlg_confirm\" onPress=\"1\">"
                                   "<keyboard><inputdata input=\"e\"/><inputdata input=\"enter\"/><inputdata input=\"np_enter\"/><inputdata input=\"mouse1\"/></keyboard>"
                                   "<xboxpad><inputdata input=\"xi_a\"/></xboxpad>"
                                   "<pspad><inputdata input=\"pad_cross\"/></pspad>"
                                   "</action>"
                                   "<action name=\"kcdc_dlg_cancel\" onPress=\"1\">"
                                   "<keyboard><inputdata input=\"escape\"/></keyboard>"
                                   "<xboxpad><inputdata input=\"xi_b\"/></xboxpad>"
                                   "<pspad><inputdata input=\"pad_circle\"/></pspad>"
                                   "</action>"
                                   "</actionmap>";

        /** The element the list belongs to, and the clips that contain it. */
        constexpr const char *kElement   = "hud";
        constexpr const char *kClipLeft  = "DialogLeft";
        constexpr const char *kClipRight = "DialogRight";

        /** CryEngine's eAAM_OnPress / eAAM_OnHold. */
        constexpr int kOnPress = 1 << 0;
        constexpr int kOnHold  = 1 << 2;

        using HudCtor_t             = void *(__fastcall *)(void *self);
        HudCtor_t g_hudCtorOriginal = nullptr;

        void *__fastcall HudCtor_Hook(void *self) {
            void *result = g_hudCtorOriginal(self);
            DialogueModule::Get().NotifyHudConstructed(static_cast<SDK::wh::guimodule::C_UIHudInteractiveDialog *>(result != nullptr ? result : self));
            return result;
        }

        using CreateAndPlaceTwins_t = bool(__fastcall *)(void *twins);
        using PlaceTwin_t           = void(__fastcall *)(void *twins, SDK::wh::entitymodule::C_Actor *actor, SDK::wh::entitymodule::C_Actor *twin);

        CreateAndPlaceTwins_t g_createAndPlaceTwinsOriginal = nullptr;
        PlaceTwin_t g_placeTwinOriginal                     = nullptr;

        /**
         * The twin setup's own answer, made false when a participant had no
         * twin to place. False is what the engine answers when a twin could
         * not be spawned, and its caller then abandons the conversation.
         */
        bool __fastcall CreateAndPlaceTwins_Hook(void *twins) {
            DialogueModule::Get().BeginTwinSetup();
            const bool created = g_createAndPlaceTwinsOriginal(twins);
            return DialogueModule::Get().EndTwinSetup(created);
        }

        /**
         * No twin, no placement. The twin is the participant soul's second
         * actor handle, read unchecked, and every placement path dereferences
         * it. The callers ignore what this returns.
         */
        void __fastcall PlaceTwin_Hook(void *twins, SDK::wh::entitymodule::C_Actor *actor, SDK::wh::entitymodule::C_Actor *twin) {
            if (twin == nullptr) {
                DialogueModule::Get().NotifyTwinMissing();
                return;
            }
            g_placeTwinOriginal(twins, actor, twin);
        }

        auto Log() {
            return Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT);
        }
    } // namespace

    DialogueModule &DialogueModule::Get() {
        static DialogueModule instance;
        return instance;
    }

    void DialogueModule::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIHudInteractiveDialog__Ctor), reinterpret_cast<LPVOID>(&HudCtor_Hook), reinterpret_cast<LPVOID *>(&g_hudCtorOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.dialogmodule__CreateAndPlaceTwins), reinterpret_cast<LPVOID>(&CreateAndPlaceTwins_Hook), reinterpret_cast<LPVOID *>(&g_createAndPlaceTwinsOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.dialogmodule__PlaceTwin), reinterpret_cast<LPVOID>(&PlaceTwin_Hook), reinterpret_cast<LPVOID *>(&g_placeTwinOriginal));
    }

    void DialogueModule::NotifyHudConstructed(SDK::wh::guimodule::C_UIHudInteractiveDialog *hud) {
        _hud = hud;
    }

    SDK::wh::dialogmodule::I_UIHudInteractiveDialog *DialogueModule::Hud() const {
        return _hud != nullptr ? const_cast<SDK::wh::guimodule::C_UIHudInteractiveDialog *>(_hud)->GetInterface() : nullptr;
    }

    bool DialogueModule::ShowPage(const Page &page) {
        SDK::wh::dialogmodule::I_UIHudInteractiveDialog *hud = Hud();
        if (hud == nullptr) {
            Log()->warn("Dialogue: a page arrived before the game built its dialogue HUD; dropping it");
            return false;
        }
        if (page.options.empty()) {
            Log()->warn("Dialogue: refusing a page with no options");
            return false;
        }

        _page = page;
        _page.options.resize(std::min(_page.options.size(), KCDC::Shared::RPC::kMaxDialogueOptions));
        _highlight = 0;

        PushRows();
        SetContainersVisible(true);
        Highlight(0);

        if (!_showing) {
            // The page draws whether or not input is available; a list nobody can
            // move is still better than nothing on screen to explain why.
            AcquireInput();
            _showing = true;
        }
        return true;
    }

    void DialogueModule::PushRows() {
        SDK::wh::dialogmodule::I_UIHudInteractiveDialog *hud = Hud();
        if (hud == nullptr) {
            return;
        }
        // The flush does not empty the staged rows, so a second page would draw
        // the first one's rows too. Clear before staging, every time.
        _hud->ClearStagedRows();

        const SDK::ScopedCryString empty("");
        const int zero = 0;
        for (const Option &option : _page.options) {
            const SDK::ScopedCryString text(option.text.c_str());
            // localize=false: the server authored this text, so it must reach the
            // movie as written rather than being looked up as a key.
            //
            // The row flag is passed false rather than the option's state. It was
            // read as "enabled" and is not: every row set true drew greyed and
            // unpickable. Its real meaning is unestablished, so this says nothing
            // with it and states the row's state below through the call the
            // element declares for exactly that.
            hud->AddDialogSequence(text.Get(), false, false, empty.Get(), 0, zero, false);
        }
        hud->SetDialogSequences(_page.onRight);

        // After the flush, because the rows have to exist to be addressed by index.
        for (std::size_t index = 0; index < _page.options.size(); ++index) {
            hud->SetEnableSequence(static_cast<int>(index), _page.options[index].enabled);
        }
    }

    void DialogueModule::Highlight(int row) {
        SDK::wh::dialogmodule::I_UIHudInteractiveDialog *hud = Hud();
        if (hud == nullptr || _page.options.empty()) {
            return;
        }
        _highlight = std::clamp(row, 0, static_cast<int>(_page.options.size()) - 1);
        hud->SetActiveSequence(_highlight, 0);
    }

    void DialogueModule::Move(int delta) {
        if (_page.options.empty()) {
            return;
        }
        const int count = static_cast<int>(_page.options.size());
        int row         = _highlight;
        // Skip disabled rows rather than letting the highlight rest on one; a
        // full lap means every row is disabled, which is the caller's problem.
        for (int step = 0; step < count; ++step) {
            row = (row + delta + count) % count;
            if (_page.options[static_cast<std::size_t>(row)].enabled) {
                break;
            }
        }
        Highlight(row);
    }

    void DialogueModule::Confirm() {
        if (!_showing || _page.options.empty()) {
            return;
        }
        const std::size_t row = static_cast<std::size_t>(_highlight);
        if (row >= _page.options.size() || !_page.options[row].enabled) {
            return;
        }
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net != nullptr) {
            KCDC::Shared::RPC::DialogueChoice choice;
            choice.sessionId   = _page.sessionId;
            choice.pageId      = _page.pageId;
            choice.optionIndex = static_cast<std::uint8_t>(row);
            net->BroadcastRPC(choice, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        // The page stays up until the server replies: it decides whether this
        // choice opens another page or ends the conversation.
    }

    void DialogueModule::Cancel() {
        if (!_showing) {
            return;
        }
        const std::uint32_t session = _page.sessionId;
        auto *net                   = Framework::CoreModules::GetNetworkPeer();
        if (net != nullptr) {
            KCDC::Shared::RPC::DialogueClose close;
            close.sessionId = session;
            close.reason    = static_cast<std::uint8_t>(KCDC::Shared::RPC::DialogueCloseReason::Cancelled);
            net->BroadcastRPC(close, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
        }
        // NOT ClosePage() here. This runs inside the action map manager's own
        // dispatch, and closing tears the map and this listener out from under
        // the loop that is still walking them -- which is an access violation
        // inside WHGame, not a tidy failure. The tick does it instead.
        _closePending = true;
    }

    void DialogueModule::ClosePage() {
        SDK::wh::dialogmodule::I_UIHudInteractiveDialog *hud = Hud();
        if (hud != nullptr) {
            // An empty staged list is what makes the flush invoke the movie's own
            // ClearDialogSequences instead of drawing rows.
            _hud->ClearStagedRows();
            hud->SetDialogSequences(_page.onRight);
            hud->ClearSkillChecks();
            hud->HideMoney();
        }
        SetContainersVisible(false);
        ReleaseInput();
        _showing      = false;
        _closePending = false;
        _page         = Page {};
        _highlight    = 0;
    }

    void DialogueModule::SetContainersVisible(bool visible) {
        if (!visible && !_containersForced) {
            return;
        }
        std::string error;
        KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(kElement, 0, error);
        if (element == nullptr) {
            Log()->warn("Dialogue: the hud element is unavailable, clip visibility unchanged: {}", error);
            return;
        }
        for (const char *clip : {kClipLeft, kClipRight}) {
            SDK::SFlashDisplayInfo info {};
            info.SetVisible(visible);
            element->SetMovieClipDisplay(clip, info);
        }
        _containersForced = visible;
    }

    bool DialogueModule::AcquireInput() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (system == nullptr || manager == nullptr) {
            Log()->warn("Dialogue: no action map manager, the page will draw but not take input");
            return false;
        }
        if (manager->GetActionMap(kMapName) != nullptr) {
            Log()->warn("Dialogue: the action map name is already owned; leaving input alone");
            return false;
        }
        SDK::XmlNodeRef node = system->LoadXmlFromBuffer(kMapXml, sizeof(kMapXml) - 1, false, false);
        if (!node.IsValid()) {
            Log()->error("Dialogue: the action map XML did not parse");
            return false;
        }
        SDK::IActionMap *map = manager->CreateActionMap(kMapName);
        if (map == nullptr || !map->LoadFromXML(node)) {
            manager->RemoveActionMap(kMapName);
            Log()->error("Dialogue: the action map could not be built");
            return false;
        }
        _manager   = manager;
        _map       = map;
        _listening = manager->AddExtraActionListener(this, kMapName);
        if (!_listening) {
            ReleaseInput();
            Log()->error("Dialogue: the action listener was refused");
            return false;
        }
        manager->EnableActionMap(kMapName, true);
        return true;
    }

    void DialogueModule::ReleaseInput() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (_map != nullptr && manager != nullptr && manager == _manager && manager->GetActionMap(kMapName) == _map) {
            manager->EnableActionMap(kMapName, false);
            if (_listening) {
                manager->RemoveExtraActionListener(this, kMapName);
            }
            manager->RemoveActionMap(kMapName);
        }
        _map       = nullptr;
        _manager   = nullptr;
        _listening = false;
    }

    void DialogueModule::OnAction(const SDK::CCryName &action, int activationMode, float) {
        if (!_listening || !_showing) {
            return;
        }
        if ((activationMode & (kOnPress | kOnHold)) == 0) {
            return;
        }
        const std::string name = action.c_str() != nullptr ? action.c_str() : "";
        if (name == "kcdc_dlg_up") {
            Move(-1);
        }
        else if (name == "kcdc_dlg_down") {
            Move(1);
        }
        else if (name == "kcdc_dlg_confirm" && (activationMode & kOnPress) != 0) {
            Confirm();
        }
        else if (name == "kcdc_dlg_cancel" && (activationMode & kOnPress) != 0) {
            Cancel();
        }
    }

    void DialogueModule::Update() {
        if (!_rpcsInstalled) {
            InstallRPCs();
        }
        // Deferred out of OnAction; see Cancel().
        if (_closePending) {
            _closePending = false;
            ClosePage();
        }
    }

    void DialogueModule::InstallRPCs() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        _rpcsInstalled = true;

        net->RegisterRPC<KCDC::Shared::RPC::DialogueShowPage>([](const KCDC::Shared::RPC::DialogueShowPage &message, MafiaNet::Packet *) {
            if (!message.IsSane()) {
                Log()->warn("Dialogue: a malformed page arrived and was dropped");
                return;
            }
            Page page;
            page.sessionId = message.sessionId;
            page.pageId    = message.pageId;
            page.onRight   = message.onRight;
            page.line      = message.line;
            for (std::uint8_t index = 0; index < message.optionCount; ++index) {
                page.options.push_back(Option {message.optionText[index], message.optionEnabled[index]});
            }
            DialogueModule::Get().ShowPage(page);
        });

        net->RegisterRPC<KCDC::Shared::RPC::DialogueClose>([](const KCDC::Shared::RPC::DialogueClose &message, MafiaNet::Packet *) {
            if (!message.IsSane()) {
                return;
            }
            DialogueModule &self = DialogueModule::Get();
            // A close for a session that is not the one on screen is a late
            // message about a conversation already gone; it must not take down
            // whatever replaced it.
            if (self.IsShowing() && self.GetSessionId() == message.sessionId) {
                self.ClosePage();
            }
        });
    }

    void DialogueModule::Reset() {
        if (_showing) {
            ClosePage();
        }
        // The HUD object outlives a session -- the game built it, not us -- so
        // it is deliberately kept. What is dropped is the conversation.
        _rpcsInstalled = false;
    }
} // namespace KCDC::Features::Dialogue
