#pragma once

#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/system/cry_name.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/types.h"
#include "sdk/wh/playermodule/c_keybinds.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace SDK {
    struct IActionFilter;
    class IActionMapManager;
} // namespace SDK

namespace KCDC::Core {
    /** A key the mod adds to the game's own controls menu. */
    enum class ModKeybind : std::uint8_t {
        EmoteWheel,
        NpcInteract,
        PushToTalk,
        VoiceTier,
        CartForward,
        CartBack,
        CartLeft,
        CartRight,
        CartTrot,
        Scoreboard,
        EndCombat,
        HintEndCombat,
        HintEndCombatHold,
        HintPrimaryAction,
        HintPrimaryActionHold,
        HintAttackSpecial,
        HintAttackSpecialHold,
        HintTorch,
        HintTorchHold,
        HintCompanionInteraction,
        HintCompanionInteractionHold,
        HintDistract,
        HintDistractHold,
        HintFollow,
        HintFollowHold,
        HintJump,
        HintJumpHold,
        HintAttackPrimary,
        HintAttackPrimaryHold,
        HintBlock,
        HintBlockHold,
        Playground,
        HintPlayground,
        Count,
    };

    /**
     * The mod's keys, declared to the game rather than watched for.
     *
     * `C_Keybinds::ParseSuperactions` is handed a document carrying a
     * `<ui_group>` of the mod's own and one `<superaction>` per binding, and
     * the matching actions are loaded into the map each binding names. The
     * engine then gives rebinding, conflict detection, reset-to-defaults and
     * profile persistence for nothing.
     *
     * The map decides when a key is live. On the game's `player` map it
     * works exactly while the player controls their character on foot; the
     * voice keys sit on a map of the mod's own that no gameplay state masks,
     * so a rider, a seated player or one in a minigame can still talk; the
     * player list sits on another at `gameplay`, live in the saddle and dark
     * in the menus.
     *
     * The bindings are a table rather than a registry a feature registers
     * into, because that document is parsed during player-module init, long
     * before any feature exists.
     *
     * Game thread only, apart from `IsHeld`.
     */
    class NativeKeybinds final: public SDK::IActionListener {
      public:
        struct Definition {
            ModKeybind binding;

            /** The action name, which is also the superaction name. */
            const char *action;

            /** What `ui_name` points at, written without the '@'. */
            const char *labelKey;

            /** What that key resolves to, in English; there is no other translation. */
            const char *label;

            /** The engine's own spelling of the default key: "g", "f2", "np_1". */
            const char *defaultInput;

            /**
             * Refuse the key while a modifier is held, so it cannot fire as
             * part of a chord meant for something else. Off for a key that
             * has to work mid-sprint, which is Shift held.
             */
            bool noModifiers;

            /** The action map the action is loaded into, which decides when the key is live. */
            const char *actionMap;
            /**
             * Whether the key joins the game's `general` conflict set. Off for
             * a key that is meant to share its default with a game key on
             * another map.
             */
            bool conflicts;

            /**
             * The game's own superaction this action is added to, instead of
             * a row of the mod's own; null for a key the mod declares itself.
             * The action then takes whatever keys the player has on that
             * superaction -- the game's defaults, their rebind, and the second
             * key the superaction carries -- and has no row, label or default
             * of its own.
             */
            const char *gameSuperaction;

            /**
             * Fire once the key has been held, as well as on press and
             * release. Only the hint actions want it: the HUD's hold ring
             * starts on that edge (`C_ActionHintManager::OnAction`, mode 4).
             */
            bool onHold = false;
        };

        static NativeKeybinds &Get();

        /**
         * The mod's map the scripted action hints are declared on. Created
         * disabled: `Features::UI::ActionHints` enables it while a hint is up,
         * because enabling it is what hands its records to the HUD.
         */
        static const char *HintActionMap();

        /**
         * The hint action that rides on the game's superaction `superaction`,
         * firing on press or once held; null when that superaction carries no
         * hint action. The superactions that do are the keys a script may
         * put a hint on.
         */
        static const Definition *FindHintAction(const char *superaction, bool hold);

        /** The hint action named `action`; null for any other action. */
        static const Definition *FindHintActionByName(const char *action);

        /** Every hint action, in declaration order. */
        static std::vector<const Definition *> HintActions();

        /** Installs the detour. Once, at load, before the player module comes up. */
        static void Install();

        /** Answers the mod's own keybind labels for the controls menu. */
        static bool ResolveLabel(const char *label, SDK::CryStringT<char> &out);

        /**
         * Registers the actions and the listener, retrying until the `player`
         * action map exists, and creates the mod's own voice map beside it.
         * Ticked from the shell, session or not: the controls menu is
         * reachable from the main menu.
         */
        void Update();

        /**
         * A mutable copy of the binding document with the mod's declarations
         * added, or an invalid handle when it could not be made. The document
         * the game supplies is a `CXMLReadOnlyNode` whose `addChild` is a
         * no-op, so this cannot write into it.
         */
        SDK::XmlNodeRef Enrich(const SDK::XmlNodeRef &document);

        /** The original parser has finished; late actions need a profile replay. */
        void CompleteDeclaration(SDK::wh::playermodule::C_Keybinds &keybinds);

        bool IsHeld(ModKeybind binding) const;

        /**
         * How many times the key went down since the last call, and resets
         * it. For a key that acts per press: a tap whose press and release
         * both land between two reads of `IsHeld` is not lost here.
         */
        std::uint32_t ConsumePresses(ModKeybind binding);

        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

        /**
         * Keeps the action `action` of the game's action maps -- the game's
         * own, such as `use`, or one of these keys, such as
         * `kcdc_npc_interact` -- from reaching anything that listens for it,
         * through an `actionFail` filter of its own. The physical key itself
         * still reaches `Key.bind`. Kept across a level change, which takes
         * the manager's filters with it: `Update` makes them again.
         */
        void SetActionBlocked(const std::string &action, bool blocked);

      private:
        /** Loads the mod's actions into their maps and starts listening. */
        bool EnsureActions();

        /** Cart controls may come up later without delaying the voice map. */
        void EnsureCartActions();

        /** Replays the game's current bindings, including explicit unbinds. */
        bool ReapplyBindings();

        /** Loads the bindings that name `actionMap` into `map`. */
        bool LoadActions(SDK::ISystem *system, SDK::IActionMap *map, const char *actionMap);

        /** Says why the keys will not work, once, and stops retrying. */
        void Refuse(const char *reason);

        std::atomic<bool> _available {false};
        std::atomic<bool> _documentParsed {false};
        bool _actionsLoaded                          = false;
        bool _cartAvailable                          = false;
        bool _cartRefused                            = false;
        bool _declarationComplete                    = false;
        bool _reapplyBindings                        = false;
        SDK::wh::playermodule::C_Keybinds *_keybinds = nullptr;

        /** Latches the first refusal, so a permanent one is not logged every frame. */
        bool _refused = false;

        /** Every blocked action and its filter, null until `Update` makes it on this manager. */
        std::map<std::string, SDK::IActionFilter *> _blocked;
        SDK::IActionMapManager *_filterManager = nullptr;

        std::atomic<bool> _held[static_cast<std::size_t>(ModKeybind::Count)] {};
        std::atomic<std::uint32_t> _presses[static_cast<std::size_t>(ModKeybind::Count)] {};
    };
} // namespace KCDC::Core
