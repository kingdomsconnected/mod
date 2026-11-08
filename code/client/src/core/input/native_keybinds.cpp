#include <utils/safe_win32.h>

#include "native_keybinds.h"

#include "sdk/cryengine/action/i_action_filter.h"
#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_player_profile_manager.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/patterns.h"
#include "sdk/wh/playermodule/c_keybinds.h"

#include <MinHook.h>
#include <array>
#include <cstring>
#include <logging/logger.h>
#include <stdexcept>
#include <string>
#include <utils/hooking/hook_function.h>

namespace KCDC::Core {
    namespace {
        constexpr const char *kLogger = "Keybinds";

        /**
         * The game's own general gameplay map: movement, camera, interaction.
         * Live exactly while the player is on foot and in control -- every
         * exclusive map above it (`horse_mounted`, `cart`, `sitting_chair`,
         * the corpse carry, dialogue, each minigame) switches it off.
         */
        constexpr char kPlayerActionMapName[] = "player";

        /**
         * The mod's own map, for keys that must outlive whatever the player
         * is doing. `CActionMapManager::RefreshActiveActionMaps` deactivates a
         * map only when an enabled exclusive map ranks strictly above it, so
         * at `platform_interrupt` the one map that can mask it is the
         * platform's own `game_interrupt_active`. A map the mod creates starts
         * disabled, hence the explicit enable in `EnsureActions`.
         */
        constexpr char kVoiceActionMapName[] = "kcdc_voice";

        /**
         * The game's own map while seated in a cart: exclusive, and holding
         * only the camera, the inventory and `cart_dismount`. Loading the
         * mod's reins into it rather than a map of the mod's own is what makes
         * them live exactly while the player sits in a cart, and never on
         * foot, where the same keys walk.
         */
        constexpr char kCartActionMapName[] = "cart";

        /**
         * The mod's own map for keys that belong to the world rather than to
         * the body: live on foot, in the saddle, on a cart bench and in a
         * chair, dark in dialogue, every minigame, the inventory and the
         * menus. `gameplay` is what buys exactly that -- `horse_mounted`,
         * `cart` and `sitting_*` are exclusive at `gameplay` too, and only a
         * strictly higher exclusive map masks a lower one, while dialogue and
         * the minigames sit at `minigames` and the inventory at `apse`.
         */
        constexpr char kHudActionMapName[] = "kcdc_hud";

        /**
         * The mod's map for the actions scripted hints are drawn for. The
         * game's action help draws a map's hints only while that map is
         * enabled, so this one starts disabled and `ActionHints` turns it on
         * while a hint is up. At `gameplay`, like the HUD map, so a hint is
         * up in the saddle and gone in dialogue, the minigames and the menus.
         */
        constexpr char kHintActionMapName[] = "kcdc_hints";

        /** A map the mod creates rather than finds, the rank it is created at, and whether it is live from the start. */
        struct ModActionMap {
            const char *name;
            const char *priority;
            bool enabled;
        };

        constexpr std::array<ModActionMap, 3> kModActionMaps {{
            {kVoiceActionMapName, "platform_interrupt", true},
            {kHudActionMapName, "gameplay", true},
            {kHintActionMapName, "gameplay", false},
        }};

        /** The mod's own map by that name, or null for one of the game's. */
        const ModActionMap *FindModActionMap(const char *actionMap) {
            for (const ModActionMap &map : kModActionMaps) {
                if (map.name == actionMap) {
                    return &map;
                }
            }
            return nullptr;
        }

        /** The section the mod's keys are listed under in the controls menu. */
        constexpr char kGroupName[]     = "kcdc";
        constexpr char kGroupLabelKey[] = "kcdc_keybind_group";
        constexpr char kGroupLabel[]    = "Multiplayer";

        /** Every label this module answers starts with it; see `ResolveLabel`. */
        constexpr char kLabelPrefix[] = "kcdc_keybind_";

        /**
         * The conflict set the game puts its own out-of-combat keys in.
         * Joining it is what makes the menu flag a mod key that has been put
         * on top of one of those, instead of letting both fire.
         */
        constexpr char kConflictName[] = "general";

        constexpr std::array<NativeKeybinds::Definition, static_cast<std::size_t>(ModKeybind::Count)> kBindings {{
            // G and T are shared with the game, so neither joins the conflict
            // set. `C_Keybinds::HasConflict` runs on the controls page's Apply
            // and Back and checks every key, not the row that changed: one
            // pair sharing a key and a conflict set anywhere makes the page
            // refuse every save, and `<include>` copies `general` into the
            // sets that hold `distract` (G) and `skiptime` (T). Sharing is
            // what players already know: `distract_init` sits on the game's
            // `crouch` map, and the skip-time dial's Wait is refused in a
            // session.
            {ModKeybind::EmoteWheel, "kcdc_emote_wheel", "kcdc_keybind_emote_wheel", "Emote wheel", "g", true, kPlayerActionMapName, false, nullptr},
            {ModKeybind::NpcInteract, "kcdc_npc_interact", "kcdc_keybind_npc_interact", "Talk to NPC", "t", true, kPlayerActionMapName, false, nullptr},
            // H, U and Y are the letters the game's keybindSuperactions.xml
            // leaves unbound; the voice keys are live on horseback and in
            // every minigame, so one the game also binds would fire both.
            // V, the old default, is companion_bond / horse_saddle_bond.
            {ModKeybind::PushToTalk, "kcdc_push_to_talk", "kcdc_keybind_push_to_talk", "Push to talk", "h", false, kVoiceActionMapName, true, nullptr},
            {ModKeybind::VoiceTier, "kcdc_voice_tier", "kcdc_keybind_voice_tier", "Voice range", "y", false, kVoiceActionMapName, true, nullptr},
            // The reins. The game has no way to drive a cart from its bench --
            // its `cart` map is the camera and the dismount -- so the mod adds
            // one to that map, where a key is live exactly while the player
            // sits in a cart. Not keys of the mod's own: the controls a horse
            // is ridden with, the game's `up`, `down`, `left`, `right` and
            // `sprint` superactions (keybindSuperactions.xml), which put the
            // player's walking keys on the rider's actions the same way --
            // `down` carries `horse_deccelerate`, `sprint` `horse_sprint`. So
            // a cart answers whatever keys the player rides with, the arrows
            // and any rebind included, on any keyboard layout. Shift is held
            // for the trot, so none of them refuses a modifier.
            {ModKeybind::CartForward, "kcdc_cart_forward", "", "", "", false, kCartActionMapName, false, "up"},
            {ModKeybind::CartBack, "kcdc_cart_back", "", "", "", false, kCartActionMapName, false, "down"},
            {ModKeybind::CartLeft, "kcdc_cart_left", "", "", "", false, kCartActionMapName, false, "left"},
            {ModKeybind::CartRight, "kcdc_cart_right", "", "", "", false, kCartActionMapName, false, "right"},
            {ModKeybind::CartTrot, "kcdc_cart_trot", "", "", "", false, kCartActionMapName, false, "sprint"},
            // Tab, as every multiplayer game has it. The game binds it too, to
            // `opponent_lock` on the combat map and `cycle_tabs` inside the
            // inventory; the inventory masks this map, and the scoreboard
            // itself waits out a lock tap in combat, so the key is shared
            // rather than flagged as a conflict. No modifiers, so Alt+Tab
            // never opens it on the way out of the game.
            {ModKeybind::Scoreboard, "kcdc_scoreboard", "kcdc_keybind_scoreboard", "Player list", "tab", true, kHudActionMapName, false, nullptr},
            {ModKeybind::EndCombat, "kcdc_end_combat", "kcdc_keybind_end_combat", "Offer to end combat", "u", false, kHudActionMapName, true, nullptr},
            {ModKeybind::Playground, "kcdc_playground", "kcdc_keybind_playground", "Playground", "f2", true, kHudActionMapName, true, nullptr},
            {ModKeybind::HintPlayground, "kcdc_hint_playground", "", "", "", true, kHintActionMapName, false, "kcdc_playground"},
            {ModKeybind::HintEndCombat, "kcdc_hint_end_combat", "", "", "", false, kHintActionMapName, false, "kcdc_end_combat"},
            {ModKeybind::HintEndCombatHold, "kcdc_hint_end_combat_hold", "", "", "", false, kHintActionMapName, false, "kcdc_end_combat", true},
            // What a scripted hint is drawn for: one action on each game key
            // a hint may name, riding on that key's superaction so it takes
            // the player's own binding, pad included, with no row of its own.
            // A press hint and a hold hint are separate actions because the
            // action help keys a record by action and gives it one type.
            {ModKeybind::HintPrimaryAction, "kcdc_hint_primary_action", "", "", "", false, kHintActionMapName, false, "primary_action"},
            {ModKeybind::HintPrimaryActionHold, "kcdc_hint_primary_action_hold", "", "", "", false, kHintActionMapName, false, "primary_action", true},
            {ModKeybind::HintAttackSpecial, "kcdc_hint_attack_special", "", "", "", false, kHintActionMapName, false, "attack_special"},
            {ModKeybind::HintAttackSpecialHold, "kcdc_hint_attack_special_hold", "", "", "", false, kHintActionMapName, false, "attack_special", true},
            {ModKeybind::HintTorch, "kcdc_hint_torch", "", "", "", false, kHintActionMapName, false, "torch"},
            {ModKeybind::HintTorchHold, "kcdc_hint_torch_hold", "", "", "", false, kHintActionMapName, false, "torch", true},
            {ModKeybind::HintCompanionInteraction, "kcdc_hint_companion_interaction", "", "", "", false, kHintActionMapName, false, "companion_interaction"},
            {ModKeybind::HintCompanionInteractionHold, "kcdc_hint_companion_interaction_hold", "", "", "", false, kHintActionMapName, false, "companion_interaction", true},
            {ModKeybind::HintDistract, "kcdc_hint_distract", "", "", "", false, kHintActionMapName, false, "distract"},
            {ModKeybind::HintDistractHold, "kcdc_hint_distract_hold", "", "", "", false, kHintActionMapName, false, "distract", true},
            {ModKeybind::HintFollow, "kcdc_hint_follow", "", "", "", false, kHintActionMapName, false, "chat_focus_follow_init"},
            {ModKeybind::HintFollowHold, "kcdc_hint_follow_hold", "", "", "", false, kHintActionMapName, false, "chat_focus_follow_init", true},
            {ModKeybind::HintJump, "kcdc_hint_jump", "", "", "", false, kHintActionMapName, false, "jump"},
            {ModKeybind::HintJumpHold, "kcdc_hint_jump_hold", "", "", "", false, kHintActionMapName, false, "jump", true},
            // The attack and the guard, for a mode that takes the mouse from
            // the player's hands -- a siege engine's operator fires and lays
            // it with them. The game's own strike and block still answer
            // these keys unless the mode filters them.
            {ModKeybind::HintAttackPrimary, "kcdc_hint_attack_primary", "", "", "", false, kHintActionMapName, false, "attack_primary"},
            {ModKeybind::HintAttackPrimaryHold, "kcdc_hint_attack_primary_hold", "", "", "", false, kHintActionMapName, false, "attack_primary", true},
            {ModKeybind::HintBlock, "kcdc_hint_block", "", "", "", false, kHintActionMapName, false, "block"},
            {ModKeybind::HintBlockHold, "kcdc_hint_block_hold", "", "", "", false, kHintActionMapName, false, "block", true},
        }};

        /**
         * One map's actions, in the shape `IActionMap::LoadFromXML` reads.
         *
         * It merges: it walks the `<action>` children, refuses by name any
         * that the map already has, and never clears what is there -- so
         * handing the game's own `player` map this document adds the mod's
         * actions to it and leaves movement, camera and interaction alone.
         *
         * `priority` and `exclusivity` are written only for the mod's own
         * map. The same function reads both off the root and overwrites the
         * map's own with whatever it finds, so naming either for `player`
         * would quietly re-rank the map that carries the player's controls.
         *
         * `_keybinds_ref_` is the game's own marker for "this key comes from
         * the keybind system": the action binds to a placeholder input no
         * device produces, and the superaction declared above is what puts a
         * real key on it. `noModifiers` keeps a mod key from firing as part
         * of a chord the player meant for something else, except where the
         * binding says it must work with one held.
         */
        std::string BuildActionsXml(const char *actionMap) {
            // Built from the bindings rather than written out, so a binding
            // cannot be declared to the controls menu without its action.
            std::string xml = "<actionmap name=\"";
            xml += actionMap;
            xml += "\"";
            if (const ModActionMap *modMap = FindModActionMap(actionMap); modMap != nullptr) {
                xml += " priority=\"";
                xml += modMap->priority;
                xml += "\" exclusivity=\"0\"";
            }
            xml += ">";
            for (const NativeKeybinds::Definition &definition : kBindings) {
                if (definition.actionMap != actionMap) {
                    continue;
                }
                xml += "<action name=\"";
                xml += definition.action;
                xml += "\" onPress=\"1\" onRelease=\"1\" keyboard=\"_keybinds_ref_\" noModifiers=\"";
                xml += definition.noModifiers ? "1" : "0";
                // The game's own Hold hints (`minigame_exit`, `close_tutorial`
                // in defaultProfile.xml) trigger at 0.25 s; the ring the hint
                // draws is what times the hold. No repeat, so it starts once.
                if (definition.onHold) {
                    xml += "\" onHold=\"1\" holdTriggerDelay=\"0.25\" holdRepeatDelay=\"-1";
                }
                xml += "\"/>";
            }
            xml += "</actionmap>";
            return xml;
        }

        constexpr int kOnPress   = 1;
        constexpr int kOnRelease = 2;

        /** The localizer hands keys over with their '@' still on. */
        const char *WithoutMarker(const char *label) {
            return label != nullptr && *label == '@' ? label + 1 : label;
        }

        /** The child of `parent` carrying `tag` and that `name`, or an invalid handle. */
        SDK::XmlNodeRef FindNamed(const SDK::XmlNodeRef &parent, const char *tag, const char *name) {
            const int count = parent->getChildCount();
            for (int index = 0; index < count; ++index) {
                SDK::XmlNodeRef child = parent->getChild(index);
                if (child.IsValid() && std::strcmp(child->getTag(), tag) == 0 && std::strcmp(child->getAttr("name"), name) == 0) {
                    return child;
                }
            }
            return {};
        }

        void DeclareGroup(const SDK::XmlNodeRef &document) {
            if (FindNamed(document, "ui_group", kGroupName).IsValid()) {
                return;
            }
            SDK::XmlNodeRef group = document->newChild("ui_group");
            if (!group.IsValid()) {
                return;
            }
            group->setAttr("name", kGroupName);
            group->setAttr("ui_label", kGroupLabelKey);
        }

        /**
         * `keyboard="writeable"` is what makes the row rebindable; the game
         * spells the other states `readable` and, by omission, fixed. Pad is
         * left off deliberately -- a row offering a controller bind that does
         * nothing is worse than no row.
         */
        /**
         * An action on one of the game's own superactions: one more
         * `<action>` child, which `C_Keybinds::Init` pushes that superaction's
         * keys onto like every other it lists.
         */
        void JoinGameSuperaction(const SDK::XmlNodeRef &document, const NativeKeybinds::Definition &definition) {
            SDK::XmlNodeRef superaction = FindNamed(document, "superaction", definition.gameSuperaction);
            if (!superaction.IsValid()) {
                Framework::Logging::GetLogger(kLogger)->warn("The game declares no superaction '{}'; '{}' will have no key", definition.gameSuperaction, definition.action);
                return;
            }
            if (FindNamed(superaction, "action", definition.action).IsValid()) {
                return;
            }
            SDK::XmlNodeRef action = superaction->newChild("action");
            if (action.IsValid()) {
                action->setAttr("name", definition.action);
                action->setAttr("map", definition.actionMap);
            }
        }

        void DeclareSuperaction(const SDK::XmlNodeRef &document, const NativeKeybinds::Definition &definition) {
            if (definition.gameSuperaction != nullptr) {
                JoinGameSuperaction(document, definition);
                return;
            }
            if (FindNamed(document, "superaction", definition.action).IsValid()) {
                return;
            }
            SDK::XmlNodeRef superaction = document->newChild("superaction");
            if (!superaction.IsValid()) {
                return;
            }
            superaction->setAttr("name", definition.action);
            superaction->setAttr("ui_group", kGroupName);
            superaction->setAttr("ui_name", definition.labelKey);
            superaction->setAttr("ui_tooltip", "");
            superaction->setAttr("keyboard", "writeable");

            SDK::XmlNodeRef action = superaction->newChild("action");
            if (action.IsValid()) {
                action->setAttr("name", definition.action);
                action->setAttr("map", definition.actionMap);
            }

            SDK::XmlNodeRef control = superaction->newChild("control");
            if (control.IsValid()) {
                control->setAttr("input", definition.defaultInput);
                control->setAttr("controller", "keyboard");
            }
        }

        void JoinConflictSet(const SDK::XmlNodeRef &document, const NativeKeybinds::Definition &definition) {
            if (!definition.conflicts || definition.gameSuperaction != nullptr) {
                return;
            }
            SDK::XmlNodeRef conflict = FindNamed(document, "conflict", kConflictName);
            if (!conflict.IsValid() || FindNamed(conflict, "superaction", definition.action).IsValid()) {
                return;
            }
            SDK::XmlNodeRef member = conflict->newChild("superaction");
            if (member.IsValid()) {
                member->setAttr("name", definition.action);
            }
        }

        using ParseSuperactions_t                       = void(__fastcall *)(SDK::wh::playermodule::C_Keybinds *, SDK::XmlNodeRef *);
        ParseSuperactions_t g_parseSuperactionsOriginal = nullptr;

        /**
         * The one place the mod's bindings enter the game.
         *
         * The original is handed an enriched copy rather than the document it
         * was called with, so everything downstream of it -- the controls
         * menu, the conflict checker, the profile writer -- sees the mod's
         * superactions as part of the game's own vocabulary. A copy that could
         * not be made is not worth failing init over: the original still gets
         * its own document and the game comes up with the mod's keys absent.
         */
        void __fastcall ParseSuperactionsDetour(SDK::wh::playermodule::C_Keybinds *keybinds, SDK::XmlNodeRef *document) {
            SDK::XmlNodeRef enriched;
            if (document != nullptr && document->IsValid()) {
                enriched = NativeKeybinds::Get().Enrich(*document);
            }

            // Before the original, not after it. What `C_Keybinds::Init`
            // does once this returns is push each superaction's key onto the
            // action it names, and an action the map does not have yet is
            // simply skipped -- leaving a row in the controls menu that
            // rebinds nothing. `Update` retries for the case where the maps
            // are not built at all this early.
            NativeKeybinds::Get().Update();

            g_parseSuperactionsOriginal(keybinds, enriched.IsValid() ? &enriched : document);
            NativeKeybinds::Get().CompleteDeclaration(*keybinds);
        }
    } // namespace

    NativeKeybinds &NativeKeybinds::Get() {
        static NativeKeybinds instance;
        return instance;
    }

    const char *NativeKeybinds::HintActionMap() {
        return kHintActionMapName;
    }

    const NativeKeybinds::Definition *NativeKeybinds::FindHintAction(const char *superaction, bool hold) {
        for (const Definition &definition : kBindings) {
            if (definition.actionMap == kHintActionMapName && definition.onHold == hold && std::strcmp(definition.gameSuperaction, superaction) == 0) {
                return &definition;
            }
        }
        return nullptr;
    }

    const NativeKeybinds::Definition *NativeKeybinds::FindHintActionByName(const char *action) {
        for (const Definition &definition : kBindings) {
            if (definition.actionMap == kHintActionMapName && std::strcmp(definition.action, action) == 0) {
                return &definition;
            }
        }
        return nullptr;
    }

    std::vector<const NativeKeybinds::Definition *> NativeKeybinds::HintActions() {
        std::vector<const Definition *> actions;
        for (const Definition &definition : kBindings) {
            if (definition.actionMap == kHintActionMapName) {
                actions.push_back(&definition);
            }
        }
        return actions;
    }

    void NativeKeybinds::Install() {
        if (MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Keybinds__ParseSuperactions), reinterpret_cast<LPVOID>(&ParseSuperactionsDetour), reinterpret_cast<LPVOID *>(&g_parseSuperactionsOriginal)) != MH_OK) {
            throw std::runtime_error("Could not install the keybind declaration hook");
        }
    }

    /**
     * On the hot path: the localizer routes every '@' key in the game through
     * here, so the prefix test comes before anything that compares whole
     * strings.
     */
    bool NativeKeybinds::ResolveLabel(const char *label, SDK::CryStringT<char> &out) {
        const char *key = WithoutMarker(label);
        if (key == nullptr || std::strncmp(key, kLabelPrefix, sizeof(kLabelPrefix) - 1) != 0) {
            return false;
        }

        if (std::strcmp(key, kGroupLabelKey) == 0) {
            out.Assign(kGroupLabel);
            return true;
        }

        for (const Definition &definition : kBindings) {
            if (definition.gameSuperaction == nullptr && std::strcmp(key, definition.labelKey) == 0) {
                out.Assign(definition.label);
                return true;
            }
        }
        return false;
    }

    SDK::XmlNodeRef NativeKeybinds::Enrich(const SDK::XmlNodeRef &document) {
        SDK::XmlNodeRef enriched = document->clone(true);
        if (!enriched.IsValid()) {
            Framework::Logging::GetLogger(kLogger)->warn("The keybind document could not be copied; the mod's keys will not appear in the controls menu");
            return {};
        }

        DeclareGroup(enriched);
        for (const Definition &definition : kBindings) {
            DeclareSuperaction(enriched, definition);
            JoinConflictSet(enriched, definition);
        }

        _documentParsed.store(true, std::memory_order_release);
        Framework::Logging::GetLogger(kLogger)->debug("Declared {} mod keybind(s) to the game's controls", kBindings.size());
        return enriched;
    }

    bool NativeKeybinds::EnsureActions() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (system == nullptr || manager == nullptr) {
            return false;
        }

        // The game builds its maps from its own profile, which is not
        // necessarily loaded when the bindings were declared. The same load
        // reads the priority names the mod's own map is ranked by.
        SDK::IActionMap *playerMap = manager->GetActionMap(kPlayerActionMapName);
        if (playerMap == nullptr) {
            return false;
        }

        // Found rather than created when it is already there: the manager
        // outlives a session, and refuses a second map of the same name.
        std::array<SDK::IActionMap *, kModActionMaps.size()> modMaps {};
        for (std::size_t index = 0; index < kModActionMaps.size(); ++index) {
            modMaps[index] = manager->GetActionMap(kModActionMaps[index].name);
            if (modMaps[index] == nullptr) {
                modMaps[index] = manager->CreateActionMap(kModActionMaps[index].name);
            }
            if (modMaps[index] == nullptr) {
                Refuse("one of the mod's own action maps could not be created");
                return false;
            }
        }

        // Tracked separately from listening, so a retry after a refused
        // listener does not load the actions again and log each as a duplicate.
        if (!_actionsLoaded) {
            bool loaded = LoadActions(system, playerMap, kPlayerActionMapName);
            for (std::size_t index = 0; index < kModActionMaps.size() && loaded; ++index) {
                loaded = LoadActions(system, modMaps[index], kModActionMaps[index].name);
            }
            if (!loaded) {
                Refuse("an action map would not take the mod's actions");
                return false;
            }
            _actionsLoaded   = true;
            _reapplyBindings = _declarationComplete;
        }

        bool listening = manager->AddExtraActionListener(this, kPlayerActionMapName);
        for (std::size_t index = 0; index < kModActionMaps.size() && listening; ++index) {
            // A hint action is heard by the game's own action help, which is
            // where `ActionHints` takes its presses from; nothing reads it here.
            if (kModActionMaps[index].name == kHintActionMapName) {
                continue;
            }
            listening = manager->AddExtraActionListener(this, kModActionMaps[index].name);
        }
        if (!listening) {
            Refuse("an action map would not take the mod's action listener");
            return false;
        }

        // Through the manager rather than the map, because only the manager
        // re-runs the exclusivity pass that decides whether it is active.
        for (std::size_t index = 0; index < kModActionMaps.size(); ++index) {
            if (!kModActionMaps[index].enabled) {
                Framework::Logging::GetLogger(kLogger)->info("The mod's action map '{}' is ready at priority {}, disabled until it is wanted", kModActionMaps[index].name, modMaps[index]->GetPriority());
                continue;
            }
            manager->EnableActionMap(kModActionMaps[index].name, true);
            Framework::Logging::GetLogger(kLogger)->info("The mod's action map '{}' is live at priority {}", kModActionMaps[index].name, modMaps[index]->GetPriority());
        }
        return true;
    }

    void NativeKeybinds::EnsureCartActions() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (system == nullptr || manager == nullptr) {
            return;
        }

        SDK::IActionMap *cartMap = manager->GetActionMap(kCartActionMapName);
        if (cartMap == nullptr) {
            return;
        }

        if (!LoadActions(system, cartMap, kCartActionMapName) || !manager->AddExtraActionListener(this, kCartActionMapName)) {
            _cartRefused = true;
            Framework::Logging::GetLogger(kLogger)->warn("The cart's keys will not work: its action map refused the actions or listener");
            return;
        }

        _cartAvailable   = true;
        _reapplyBindings = _reapplyBindings || _declarationComplete;
    }

    void NativeKeybinds::CompleteDeclaration(SDK::wh::playermodule::C_Keybinds &keybinds) {
        _keybinds            = &keybinds;
        _declarationComplete = true;
        // Init still has to load the saved profile after this detour returns.
        // Replay only on a later shell tick, once those choices are current.
    }

    bool NativeKeybinds::ReapplyBindings() {
        SDK::IGameFramework *framework      = SDK::IGameFramework::GetInstance();
        SDK::IPlayerProfileManager *manager = framework != nullptr ? framework->GetIPlayerProfileManager() : nullptr;
        if (manager == nullptr) {
            return false;
        }

        const char *user             = manager->GetCurrentUser();
        SDK::IPlayerProfile *profile = user != nullptr ? manager->GetCurrentProfile(user) : nullptr;
        if (profile == nullptr) {
            return false;
        }

        // Snapshot the current choices into the in-memory profile first, so
        // a late cart map cannot roll back an unsaved rebind. Neither callback
        // writes a file; only the profile manager's SaveProfile does that.
        _keybinds->ReapplyBindings(profile);
        Framework::Logging::GetLogger(kLogger)->info("Reapplied the game's keybinds after the mod's action maps became available");
        return true;
    }

    bool NativeKeybinds::LoadActions(SDK::ISystem *system, SDK::IActionMap *map, const char *actionMap) {
        const std::string document = BuildActionsXml(actionMap);
        SDK::XmlNodeRef actions    = system->LoadXmlFromBuffer(document.c_str(), document.size(), false, false);
        return actions.IsValid() && map->LoadFromXML(actions);
    }

    /**
     * A refusal here is permanent -- the map exists and has said no -- so it
     * is said once and then stops being retried.
     */
    void NativeKeybinds::Refuse(const char *reason) {
        if (!_refused) {
            _refused = true;
            Framework::Logging::GetLogger(kLogger)->warn("The mod's keys will not work: {}", reason);
        }
    }

    namespace {
        /** Each blocked action has a filter to itself, so any reason will do. */
        constexpr std::uint8_t kBlockReason = 0;

        SDK::IActionFilter *CreateBlock(SDK::IActionMapManager *manager, const std::string &action) {
            const std::string name     = "kcdc_block_" + action;
            const std::string xml      = "<actionfilter name=\"" + name + "\" type=\"actionFail\"><action name=\"" + action + "\"/></actionfilter>";
            SDK::IActionFilter *filter = manager->FindOrCreateActionFilter(name.c_str(), xml.c_str(), xml.size());
            if (filter != nullptr) {
                filter->Enable(kBlockReason);
            }
            return filter;
        }
    } // namespace

    void NativeKeybinds::SetActionBlocked(const std::string &action, bool blocked) {
        if (blocked) {
            _blocked.try_emplace(action, nullptr);
            return;
        }
        const auto found = _blocked.find(action);
        if (found == _blocked.end()) {
            return;
        }
        // A filter made on a manager a level change has since replaced went with it.
        if (found->second != nullptr && SDK::IActionMapManager::GetInstance() == _filterManager) {
            found->second->Disable(kBlockReason, true);
        }
        _blocked.erase(found);
    }

    void NativeKeybinds::Update() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (manager != _filterManager) {
            _filterManager = manager;
            for (auto &[action, filter] : _blocked) {
                filter = nullptr;
            }
        }
        if (manager != nullptr) {
            for (auto &[action, filter] : _blocked) {
                if (filter == nullptr) {
                    filter = CreateBlock(manager, action);
                }
            }
        }

        if (_refused || !_documentParsed.load(std::memory_order_acquire)) {
            return;
        }
        if (!_available.load(std::memory_order_acquire) && EnsureActions()) {
            _available.store(true, std::memory_order_release);
        }
        if (!_cartAvailable && !_cartRefused) {
            EnsureCartActions();
        }
        if (_declarationComplete && _reapplyBindings && ReapplyBindings()) {
            _reapplyBindings = false;
        }
    }

    bool NativeKeybinds::IsHeld(ModKeybind binding) const {
        return _held[static_cast<std::size_t>(binding)].load(std::memory_order_acquire);
    }

    std::uint32_t NativeKeybinds::ConsumePresses(ModKeybind binding) {
        return _presses[static_cast<std::size_t>(binding)].exchange(0, std::memory_order_acq_rel);
    }

    /**
     * Runs inside the engine's input dispatch, so it only records. What a
     * binding means is decided by whoever reads it, on their own tick.
     */
    void NativeKeybinds::OnAction(const SDK::CCryName &action, int activationMode, float) {
        const char *name = action.c_str();
        for (const Definition &definition : kBindings) {
            if (std::strcmp(name, definition.action) != 0) {
                continue;
            }

            const std::size_t slot = static_cast<std::size_t>(definition.binding);
            if ((activationMode & kOnPress) != 0) {
                _held[slot].store(true, std::memory_order_release);
                _presses[slot].fetch_add(1, std::memory_order_acq_rel);
            }
            if ((activationMode & kOnRelease) != 0) {
                _held[slot].store(false, std::memory_order_release);
            }
            return;
        }
    }
} // namespace KCDC::Core

static InitFunction init(
    []() {
        KCDC::Core::NativeKeybinds::Install();
    },
    "Keybinds");
