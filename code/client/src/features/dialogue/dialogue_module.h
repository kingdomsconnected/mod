#pragma once

#include "core/feature_registry.h"

#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/wh/dialogmodule/i_ui_hud_interactive_dialog.h"
#include "sdk/wh/guimodule/c_ui_hud_interactive_dialog.h"

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Dialogue {
    /** One row of a page, as the server authored it. */
    struct Option {
        std::string text;
        bool enabled = true;
    };

    /** One page: a line, its options, and which side to draw on. */
    struct Page {
        std::uint32_t sessionId = 0;
        std::uint32_t pageId    = 0;
        bool onRight            = false;

        /**
         * Carried, not yet drawn. The line belongs to the subtitle widget, which
         * is a separate piece of the HUD without an SDK interface yet. The line
         * travels on the wire so the server API can remain unchanged.
         */
        std::string line;

        std::vector<Option> options;
    };

    /**
     * The client half of the conversation system: it draws a page in the game's
     * own dialogue HUD, owns the highlighted row, and reports the answer.
     *
     * **It drives the game's object, not the movie.** The rows go in through
     * `I_UIHudInteractiveDialog::AddDialogSequence` and are flushed with
     * `SetDialogSequences`, which is exactly the pair the game's own dialogue
     * system uses -- so the row bookkeeping, the money readout gate and the
     * skill-check list all behave as the game expects them to.
     *
     * There is no accessor for that object: the GUI module builds it through
     * RTTR reflection and keeps the shared_ptr in a type-keyed registry. So the
     * instance arrives from a hook on its constructor, which is why
     * `NotifyHudConstructed` exists and why nothing here searches for it.
     *
     * The one thing the game's own calls do **not** do is make the list's
     * container visible: `DialogLeft` and `DialogRight` are declared clips that
     * stay hidden until something else shows them, and populating the rows does
     * not. So this shows them while a page is up and restores them afterwards,
     * through the element's own declared movie-clip API.
     *
     * Input is taken the way the mod already takes it elsewhere: an exclusive
     * action map of our own plus `AddExtraActionListener`. The game's `dialog`
     * map is not reused -- it is bound to the game's own dialogue controller,
     * and borrowing it would put two owners on one selection index.
     *
     * Game-thread only. Every method touches the HUD object or the action map.
     */
    class DialogueModule final
        : public KCDC::Core::IFeatureService
        , public SDK::IActionListener {
      public:
        static DialogueModule &Get();

        /** Installs the constructor hook and the twin-setup detours. Called once, at boot. */
        static void Install();

        /**
         * The game's own conversation, not ours: before it starts, it stands
         * a spawned "twin" in for each participant, and the twin shares the
         * participant's soul, filling the soul's second actor handle.
         * `CreateAndPlaceTwins` answers false when a twin could not be made,
         * and the conversation is then abandoned -- but its placing pass has
         * already run for every participant by then, and dereferences the
         * missing twin (WHGame+0xC89CAD).
         *
         * So a participant that reaches placement with no twin is not placed,
         * and it fails the setup the way a twin that could not be spawned
         * does. Both calls run on one stack, inside the setup.
         */
        void BeginTwinSetup() {
            _twinMissing = false;
        }
        void NotifyTwinMissing() {
            _twinMissing = true;
        }
        bool EndTwinSetup(bool created) const {
            return created && !_twinMissing;
        }

        void Update() override;
        void Reset() override;

        /** The game built its interactive-dialogue HUD; remember it. */
        void NotifyHudConstructed(SDK::wh::guimodule::C_UIHudInteractiveDialog *hud);

        /** Draws `page`, replacing whatever was up. False if the HUD is not available yet. */
        bool ShowPage(const Page &page);

        /** Takes the page down and hands input back. */
        void ClosePage();

        bool IsShowing() const {
            return _showing;
        }
        std::uint32_t GetSessionId() const {
            return _page.sessionId;
        }

        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

      private:
        void InstallRPCs();
        /** The game's object, or null until its constructor has run. */
        SDK::wh::dialogmodule::I_UIHudInteractiveDialog *Hud() const;

        void PushRows();
        void Highlight(int row);
        void Move(int delta);
        void Confirm();
        void Cancel();

        /** The list's container clips, which populating the rows does not reveal. */
        void SetContainersVisible(bool visible);

        bool AcquireInput();
        void ReleaseInput();

        SDK::wh::guimodule::C_UIHudInteractiveDialog *_hud = nullptr;

        Page _page;
        bool _showing  = false;
        int _highlight = 0;

        SDK::IActionMap *_map            = nullptr;
        SDK::IActionMapManager *_manager = nullptr;
        bool _listening                  = false;
        bool _containersForced           = false;
        bool _rpcsInstalled              = false;
        /** Set from OnAction, acted on by the tick. See Cancel(). */
        bool _closePending = false;
        /** A participant of the native setup under way had no twin. */
        bool _twinMissing = false;
    };
} // namespace KCDC::Features::Dialogue
