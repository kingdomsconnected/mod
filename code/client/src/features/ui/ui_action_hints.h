#pragma once

#include "core/feature_registry.h"
#include "ui_hint_input_queue.h"
#include "core/input/native_keybinds.h"
#include "sdk/cryengine/system/cry_name.h"
#include "sdk/wh/playermodule/c_action_hint_manager.h"
#include "sdk/wh/playermodule/c_action_sets.h"

#include <function2/function2.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::UI {
    /**
     * Script-owned "Press [key] to ..." hints, drawn by the game's own action
     * help rather than by anything the mod renders.
     *
     * A hint names one of the game's keys by its superaction. The key is
     * whatever the player has bound to it, so the glyph follows a rebind and
     * a pad without the hint knowing. Behind each key sit two actions on the
     * mod's `kcdc_hints` map -- one firing on press, one once held -- and a
     * record for each in the action help (`C_ActionSets`), parsed in the
     * first time a hint is wanted. Showing a hint is setting its record's text
     * and visibility, and enabling the map, which is what hands the records to
     * `C_ActionHintManager`; the HUD then draws them as it draws its own.
     *
     * Two hints on one key stack: the newest is the one drawn and the one a
     * press goes to, and removing it brings back the one under it. A hint on
     * a key the game also uses does not stop the game's own action.
     *
     * A press is caught where the game resolves it, in
     * `C_ActionHintManager::HandleAction`, which also carries the end of a
     * hold ring. That runs inside the engine's input dispatch under its lock,
     * so it only records; the press reaches the owner on this service's tick.
     *
     * Game thread only, apart from the press queue.
     */
    class ActionHints final: public KCDC::Core::IFeatureService {
      public:
        enum class Mode : std::uint8_t {
            Press,
            Hold
        };

        struct Options {
            /** The game superaction whose key the hint is on: `primary_action`, `jump`... */
            std::string key;
            Mode mode = Mode::Press;
            std::string text;
            bool enabled = true;
            /** Seconds a hold hint's ring takes to fill. Ignored for a press hint. */
            float holdDuration = 1.0f;
        };

        /** Told which hint fired, on the game thread, outside the engine's input dispatch. */
        using PressSink = fu2::function<void(std::uint32_t hint) const>;

        static ActionHints &Get();

        /** Installs the press detour. Once, at load. */
        static void Install();

        /** The keys a hint may name, in a fixed order. */
        static std::vector<std::string> Keys();

        /**
         * A new hint, drawn from the next tick; 0 when `options.key` is not
         * one of `Keys`. A hint with its own `sink` is the mod's: its presses
         * go there rather than to the scripting sink.
         */
        std::uint32_t Create(const std::string &resource, const Options &options, PressSink sink = {});

        bool SetText(std::uint32_t hint, const std::string &text);
        bool SetEnabled(std::uint32_t hint, bool enabled);
        bool Remove(std::uint32_t hint);

        /** Whether a hint on `superaction`'s key is drawn, as last published. */
        bool IsShown(const char *superaction) const;

        /**
         * The keys, by superaction, of every hint the HUD draws right now --
         * the game's own, interaction prompts, other scripts' and the mod's
         * -- as of the engine's last refresh. A game hint is reported by its
         * action name, which is the superaction it rides on.
         */
        static std::vector<std::string> ShownKeys();

        /** Whether any drawn hint, from whoever, is on `superaction`'s key. */
        static bool IsKeyInUse(const char *superaction);

        /** One hint the HUD draws, as `DrawnHints` reports it. */
        struct Drawn {
            /** The superaction a script's hint rides on, or the action a game hint triggers (`talk`, `use`). */
            std::string key;
            /** The action map the hint belongs to: `player` for the world's prompts, a menu screen's own (`apse`, `apse_inventory`) for a menu's. */
            std::string map;
            /** What the HUD writes beside the key, in the player's language. */
            std::string text;
            Mode mode    = Mode::Press;
            bool enabled = true;
            /** How far a hold hint's ring has filled, 0 to 1; 0 while it is not held. */
            float progress = 0.0f;
            /** Who the prompt is about, as the game names them -- a person or an animal; empty for anything else. */
            std::string target;

            bool operator==(const Drawn &) const = default;
        };

        /**
         * Every hint the HUD draws right now -- the game's own, interaction
         * prompts, scripts' and the mod's -- in the HUD's order, as of the
         * engine's last refresh. What a script needs to draw them itself.
         */
        static std::vector<Drawn> DrawnHints();

        /** Takes down every hint `resource` put up. */
        void Release(const std::string &resource);

        void SetPressSink(PressSink sink);

        void Update() override;
        void Reset() override;

      private:
        struct Hint {
            std::string resource;
            /** Which key, and whether it fires on press or once held. */
            const KCDC::Core::NativeKeybinds::Definition *action = nullptr;
            std::string text;
            bool enabled       = true;
            float holdDuration = 1.0f;
            PressSink sink;
        };

        /** What `DrawnHints` returned on the last tick, so a change raises `actionHintsChanged` once. */
        std::vector<Drawn> _drawn;

        /** One hint action: the hints stacked on it, newest last, and what the action help was last told. */
        struct Slot {
            std::vector<std::uint32_t> stack;
            bool dirty = false;
            bool shown = false;
        };

        /** Calls `visit(key)` for each drawn hint's key until it returns true. */
        /** Visits each record the HUD draws with the key it is on; stops when `visit` returns true. */
        template <typename Visit> static void ForEachDrawnKey(Visit &&visit);

        /** Reads the mod's records into the action help, once per `C_ActionSets`. */
        bool EnsureActionHelp(SDK::wh::playermodule::C_ActionSets *actionSets);

        /** Tells the action help what the top of one slot's stack is, or that it has none. */
        void Apply(SDK::wh::playermodule::I_ActionSets *actionSets, const KCDC::Core::NativeKeybinds::Definition &action, Slot &slot);

        void MarkDirty(std::uint32_t hint);

        /** Brings the action help and the map's enabled state in line with the stacks, once both exist. */
        void Publish();
        void DispatchPresses();

        static void __fastcall HandleActionHook(SDK::wh::playermodule::C_ActionHintManager *manager, const SDK::CCryName &action, std::uint8_t hintType);

        std::map<std::uint32_t, Hint> _hints;
        std::map<const KCDC::Core::NativeKeybinds::Definition *, Slot> _slots;
        std::uint32_t _nextId = 1;

        SDK::wh::playermodule::C_ActionSets *_parsedInto = nullptr;
        bool _mapEnabled                                 = false;
        PressSink _pressSink;

        /** Filled inside the engine's input dispatch, drained on the tick. */
        HintInputQueue _input;

        // The hint type is a byte: callers set only the low byte of r8.
        void(__fastcall *_handleActionOriginal)(SDK::wh::playermodule::C_ActionHintManager *, const SDK::CCryName &, std::uint8_t) = nullptr;
    };
} // namespace KCDC::Features::UI
