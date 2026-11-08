#include <utils/safe_win32.h>

#include "ui_action_hints.h"

#include "features/ui/ui_action_hints_scripting.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/system/i_localization_manager.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_player_interactor.h"
#include "sdk/wh/playermodule/c_player_module.h"

#include <MinHook.h>
#include <algorithm>
#include <cstring>
#include <logging/logger.h>
#include <sstream>
#include <utils/hooking/hook_function.h>

namespace KCDC::Features::UI {
    namespace {
        constexpr const char *kLogger = "ActionHints";

        /** The hint types `C_ActionHintManager::HandleAction` is given; see defaultActionHelp.xml. */
        constexpr std::uint8_t kHintTypePress = 0;
        constexpr std::uint8_t kHintTypeHold  = 2;

        /** Every hint action is named with it, which is what lets the press detour skip the game's own hints cheaply. */
        constexpr char kHintActionPrefix[] = "kcdc_hint_";

        /**
         * One record per hint action, in the format defaultActionHelp.xml
         * documents in its own header. Hidden and textless until a script
         * puts a hint on it; `order` keeps them after the interactor's
         * prompts, which are 0, and a hold record's ring time is replaced
         * per hint.
         */
        std::string BuildActionHelpXml() {
            std::string xml = "<action_help><action_help_set actionmap=\"";
            xml += KCDC::Core::NativeKeybinds::HintActionMap();
            xml += "\">";
            for (const KCDC::Core::NativeKeybinds::Definition *action : KCDC::Core::NativeKeybinds::HintActions()) {
                xml += "<hint text=\"\" action=\"";
                xml += action->action;
                xml += action->onHold ? "\" type=\"Hold\" hold_duration=\"1\"" : "\" type=\"Press\"";
                xml += " visible=\"false\" order=\"50\"/>";
            }
            xml += "</action_help_set></action_help>";
            return xml;
        }

        SDK::wh::playermodule::I_ActionSets *FindActionSets() {
            SDK::wh::C_GlobalEnvironment *environment           = SDK::wh::C_GlobalEnvironment::GetInstance();
            SDK::wh::playermodule::C_PlayerModule *playerModule = environment != nullptr ? environment->GetPlayerModule() : nullptr;
            return playerModule != nullptr ? playerModule->GetActionSets() : nullptr;
        }
    } // namespace

    ActionHints &ActionHints::Get() {
        static ActionHints instance;
        return instance;
    }

    void ActionHints::Install() {
        if (MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_ActionHintManager__HandleAction), reinterpret_cast<LPVOID>(&HandleActionHook), reinterpret_cast<LPVOID *>(&Get()._handleActionOriginal)) != MH_OK) {
            Framework::Logging::GetLogger(kLogger)->error("Could not install the action hint press hook; scripted hints will show but never fire");
        }
    }

    /**
     * Inside the engine's input dispatch, under its lock: record and leave.
     * Every hint the game resolves comes through here, so the prefix test is
     * the whole cost for the game's own.
     */
    void __fastcall ActionHints::HandleActionHook(SDK::wh::playermodule::C_ActionHintManager *manager, const SDK::CCryName &action, std::uint8_t hintType) {
        ActionHints &self = Get();
        const char *name  = action.c_str();
        if (std::strncmp(name, kHintActionPrefix, sizeof(kHintActionPrefix) - 1) == 0) {
            self._input.Capture(name, hintType);
        }
        self._handleActionOriginal(manager, action, hintType);
    }

    std::vector<std::string> ActionHints::Keys() {
        std::vector<std::string> keys;
        for (const KCDC::Core::NativeKeybinds::Definition *action : KCDC::Core::NativeKeybinds::HintActions()) {
            if (!action->onHold) {
                keys.emplace_back(action->gameSuperaction);
            }
        }
        return keys;
    }

    std::uint32_t ActionHints::Create(const std::string &resource, const Options &options, PressSink sink) {
        const KCDC::Core::NativeKeybinds::Definition *action = KCDC::Core::NativeKeybinds::FindHintAction(options.key.c_str(), options.mode == Mode::Hold);
        if (action == nullptr) {
            return 0;
        }

        const std::uint32_t id = _nextId++;
        _hints[id]             = Hint {resource, action, options.text, options.enabled, options.holdDuration, std::move(sink)};

        Slot &slot = _slots[action];
        slot.stack.push_back(id);
        slot.dirty = true;
        return id;
    }

    void ActionHints::MarkDirty(std::uint32_t hint) {
        const auto entry = _hints.find(hint);
        if (entry == _hints.end()) {
            return;
        }
        Slot &slot = _slots[entry->second.action];
        // Only the top of a stack is drawn; one under it changes nothing yet.
        if (!slot.stack.empty() && slot.stack.back() == hint) {
            slot.dirty = true;
        }
    }

    bool ActionHints::SetText(std::uint32_t hint, const std::string &text) {
        const auto entry = _hints.find(hint);
        if (entry == _hints.end()) {
            return false;
        }
        if (entry->second.text != text) {
            entry->second.text = text;
            MarkDirty(hint);
        }
        return true;
    }

    bool ActionHints::SetEnabled(std::uint32_t hint, bool enabled) {
        const auto entry = _hints.find(hint);
        if (entry == _hints.end()) {
            return false;
        }
        if (entry->second.enabled != enabled) {
            entry->second.enabled = enabled;
            MarkDirty(hint);
        }
        return true;
    }

    bool ActionHints::IsShown(const char *superaction) const {
        for (const auto &[action, slot] : _slots) {
            if (slot.shown && std::strcmp(action->gameSuperaction, superaction) == 0) {
                return true;
            }
        }
        return false;
    }

    template <typename Visit> void ActionHints::ForEachDrawnKey(Visit &&visit) {
        SDK::wh::C_GlobalEnvironment *environment                 = SDK::wh::C_GlobalEnvironment::GetInstance();
        SDK::wh::playermodule::C_PlayerModule *playerModule       = environment != nullptr ? environment->GetPlayerModule() : nullptr;
        const SDK::wh::playermodule::C_ActionHintManager *manager = playerModule != nullptr ? playerModule->GetActionHintManager() : nullptr;
        if (manager == nullptr) {
            return;
        }

        // RefreshActionHints' own filter: visible, or the action whose hold
        // ring is filling.
        const char *held = manager->GetHeldAction();
        for (const auto *record = manager->GetRecordsBegin(); record != manager->GetRecordsEnd(); ++record) {
            const char *action = record->m_action.m_pData;
            if (action == nullptr) {
                continue;
            }
            const bool drawn = record->m_uiVisible || (record->m_ptr00D0 == nullptr && held != nullptr && std::strcmp(action, held) == 0);
            if (!drawn) {
                continue;
            }
            // The mod's records name a hint action; report the key it rides on.
            const KCDC::Core::NativeKeybinds::Definition *hint = KCDC::Core::NativeKeybinds::FindHintActionByName(action);
            if (visit(*record, hint != nullptr ? hint->gameSuperaction : action)) {
                return;
            }
        }
    }

    std::vector<std::string> ActionHints::ShownKeys() {
        std::vector<std::string> keys;
        ForEachDrawnKey([&keys](const SDK::wh::entitymodule::S_InteractorActionRecord &, const char *key) {
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.emplace_back(key);
            }
            return false;
        });
        return keys;
    }

    bool ActionHints::IsKeyInUse(const char *superaction) {
        bool inUse = false;
        ForEachDrawnKey([&](const SDK::wh::entitymodule::S_InteractorActionRecord &, const char *key) {
            inUse = std::strcmp(key, superaction) == 0;
            return inUse;
        });
        return inUse;
    }

    std::vector<ActionHints::Drawn> ActionHints::DrawnHints() {
        SDK::wh::C_GlobalEnvironment *environment                 = SDK::wh::C_GlobalEnvironment::GetInstance();
        SDK::wh::playermodule::C_PlayerModule *playerModule       = environment != nullptr ? environment->GetPlayerModule() : nullptr;
        const SDK::wh::playermodule::C_ActionHintManager *manager = playerModule != nullptr ? playerModule->GetActionHintManager() : nullptr;
        const char *held                                          = manager != nullptr ? manager->GetHeldAction() : nullptr;
        SDK::ILocalizationManager *strings                        = SDK::ILocalizationManager::GetInstance();
        SDK::IActorSystem *actors                                 = SDK::IActorSystem::GetInstance();
        const auto localize                                       = [strings](const char *label) {
            SDK::ScopedCryString localized("");
            return strings != nullptr && strings->LocalizeLabel(label, *localized.Out()) ? std::string(localized.c_str()) : std::string();
        };

        std::vector<std::pair<std::int32_t, Drawn>> ordered;
        ForEachDrawnKey([&](const SDK::wh::entitymodule::S_InteractorActionRecord &record, const char *key) {
            Drawn drawn;
            drawn.key        = key;
            drawn.map        = record.m_actionMap.m_pData != nullptr ? record.m_actionMap.m_pData : "";
            drawn.mode       = record.m_hintType == kHintTypeHold ? Mode::Hold : Mode::Press;
            drawn.enabled    = record.m_enabled;
            const char *text = record.m_hint.m_pData != nullptr ? record.m_hint.m_pData : "";
            // The game's own hints carry string-table labels, with the `@` or without, and can carry several:
            // AnimStash.lua appends " ui_empty_stash" to an empty stash's "@ui_open_stash". The HUD puts `@` on
            // each word and translates each alone, so this does too. The mod's carry their text as written, so
            // a hint with any word that is no label keeps its text.
            std::istringstream words(text);
            for (std::string word; words >> word;) {
                const std::string translated = localize((word[0] == '@' ? word : "@" + word).c_str());
                if (translated.empty()) {
                    drawn.text.clear();
                    break;
                }
                drawn.text += drawn.text.empty() ? translated : " " + translated;
            }
            if (drawn.text.empty()) {
                drawn.text = text;
            }
            // A person or an animal is named by its soul, which holds a string-table key -- an NPC of
            // the server's included, whose key the localization detour answers with its name.
            auto *actor                            = actors != nullptr && record.m_targetEntityId != 0 ? static_cast<SDK::wh::entitymodule::C_Actor *>(actors->GetActor(record.m_targetEntityId)) : nullptr;
            const SDK::wh::rpgmodule::C_Soul *soul = actor != nullptr ? actor->GetSoul() : nullptr;
            const char *nameKey                    = soul != nullptr ? soul->GetUIName() : nullptr;
            if (nameKey != nullptr && nameKey[0] != '\0') {
                drawn.target = localize((std::string("@") + nameKey).c_str());
            }
            if (drawn.mode == Mode::Hold && held != nullptr && std::strcmp(record.m_action.m_pData, held) == 0) {
                drawn.progress = std::clamp(manager->GetHeldProgress(), 0.0f, 1.0f);
            }
            ordered.emplace_back(record.m_uiOrder, std::move(drawn));
            return false;
        });
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
        std::vector<Drawn> drawn;
        drawn.reserve(ordered.size());
        for (auto &[order, hint] : ordered) {
            drawn.push_back(std::move(hint));
        }
        return drawn;
    }

    bool ActionHints::Remove(std::uint32_t hint) {
        const auto entry = _hints.find(hint);
        if (entry == _hints.end()) {
            return false;
        }
        Slot &slot = _slots[entry->second.action];
        slot.stack.erase(std::remove(slot.stack.begin(), slot.stack.end(), hint), slot.stack.end());
        slot.dirty = true;
        _hints.erase(entry);
        return true;
    }

    void ActionHints::Release(const std::string &resource) {
        std::vector<std::uint32_t> owned;
        for (const auto &[id, hint] : _hints) {
            if (hint.resource == resource) {
                owned.push_back(id);
            }
        }
        for (const std::uint32_t id : owned) {
            Remove(id);
        }
    }

    void ActionHints::SetPressSink(PressSink sink) {
        _pressSink = std::move(sink);
    }

    bool ActionHints::EnsureActionHelp(SDK::wh::playermodule::C_ActionSets *actionSets) {
        if (_parsedInto == actionSets) {
            return true;
        }

        SDK::ISystem *system = SDK::ISystem::GetInstance();
        if (system == nullptr) {
            return false;
        }

        const std::string document = BuildActionHelpXml();
        const SDK::XmlNodeRef help = system->LoadXmlFromBuffer(document.c_str(), document.size(), false, false);
        if (!help.IsValid()) {
            return false;
        }

        actionSets->ParseActionHelp(help);
        _parsedInto = actionSets;

        _input.Reset();
        // Fresh records know nothing of the hints already up.
        for (auto &[action, slot] : _slots) {
            slot.dirty = true;
            slot.shown = false;
        }
        Framework::Logging::GetLogger(kLogger)->info("Declared {} hint record(s) to the game's action help", KCDC::Core::NativeKeybinds::HintActions().size());
        return true;
    }

    void ActionHints::Apply(SDK::wh::playermodule::I_ActionSets *actionSets, const KCDC::Core::NativeKeybinds::Definition &action, Slot &slot) {
        const SDK::ScopedCryString map(KCDC::Core::NativeKeybinds::HintActionMap());
        const SDK::ScopedCryString name(action.action);

        slot.dirty = false;
        if (slot.stack.empty()) {
            _input.SetTarget(action.action, 0, 0);
            if (slot.shown) {
                actionSets->SetActionVisible(map, name, false, false);
                slot.shown = false;
            }
            return;
        }

        const Hint &hint = _hints.at(slot.stack.back());
        const SDK::ScopedCryString text(hint.text.c_str());
        // Not localized: the text reaches the movie as the script wrote it.
        actionSets->SetActionHint(map, name, text, false, false);
        actionSets->SetActionEnabled(map, name, hint.enabled, false);
        if (action.onHold) {
            actionSets->SetActionHoldDuration(map, name, hint.holdDuration, false);
        }
        actionSets->SetActionVisible(map, name, true, false);
        slot.shown = true;
        _input.SetTarget(action.action, hint.enabled ? slot.stack.back() : 0, action.onHold ? kHintTypeHold : kHintTypePress);
    }

    void ActionHints::Publish() {
        // Every tick of a session comes through here; a frame where no hint
        // changed touches nothing native.
        if (std::none_of(_slots.begin(), _slots.end(), [](const auto &entry) {
                return entry.second.dirty;
            })) {
            return;
        }

        SDK::IActionMapManager *manager                 = SDK::IActionMapManager::GetInstance();
        SDK::wh::playermodule::I_ActionSets *actionSets = FindActionSets();
        // The map is created by the keybinds once the game's own maps exist;
        // until then there is nothing to enable and no key behind a record.
        if (manager == nullptr || actionSets == nullptr || manager->GetActionMap(KCDC::Core::NativeKeybinds::HintActionMap()) == nullptr) {
            return;
        }
        if (!EnsureActionHelp(static_cast<SDK::wh::playermodule::C_ActionSets *>(actionSets))) {
            return;
        }

        bool anyShown = false;
        for (auto &[action, slot] : _slots) {
            if (slot.dirty) {
                Apply(actionSets, *action, slot);
            }
            anyShown = anyShown || slot.shown;
        }

        // Enabling the map is what hands its records to the HUD, and
        // disabling it takes them back; a map with nothing to show is off, so
        // its keys cost the game nothing either.
        if (anyShown != _mapEnabled) {
            manager->EnableActionMap(KCDC::Core::NativeKeybinds::HintActionMap(), anyShown);
            _mapEnabled = anyShown;
            Framework::Logging::GetLogger(kLogger)->debug("Hint map {}", anyShown ? "enabled" : "disabled");
        }
    }

    /**
     * A press goes to the hint on top when it was resolved, if it is still
     * there and enabled: a press that crossed a removal is dropped, never
     * handed to the hint underneath.
     */
    void ActionHints::DispatchPresses() {
        for (const auto id : _input.Drain()) {
            const auto hint = _hints.find(id);
            if (hint == _hints.end() || !hint->second.enabled) {
                continue;
            }
            const auto slot = _slots.find(hint->second.action);
            if (slot == _slots.end() || !slot->second.shown || slot->second.stack.empty() || slot->second.stack.back() != id) {
                continue;
            }

            Framework::Logging::GetLogger(kLogger)->debug("Hint {} fired on '{}'", id, hint->second.action->action);
            // Last: the sink may run script, which may add or remove hints --
            // including the next queued target. Its captured ID is never
            // reassigned to a new top. Copied, as the sink may remove its own.
            const PressSink sink = hint->second.sink ? hint->second.sink : _pressSink;
            if (sink) {
                sink(id);
            }
        }
    }

    void ActionHints::Update() {
        DispatchPresses();
        Publish();
        std::vector<Drawn> drawn = DrawnHints();
        if (drawn != _drawn) {
            _drawn = std::move(drawn);
            Scripting::Events::ActionHintsChanged();
        }
    }

    void ActionHints::Reset() {
        _hints.clear();
        for (auto &[action, slot] : _slots) {
            slot.stack.clear();
            slot.dirty = true;
        }
        _input.Reset();
        _drawn.clear();
        Publish();
    }
} // namespace KCDC::Features::UI

static InitFunction init(
    []() {
        KCDC::Features::UI::ActionHints::Install();
    },
    "UI");
