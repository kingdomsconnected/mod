#pragma once

#include "core/feature_registry.h"
#include "core/ui/native_ui/native_ui.h"

#include "sdk/cryengine/action/i_action_filter.h"
#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/system/cry_name.h"

#include "shared/features/social/social_scoreboard_rpc.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::Social {
    /**
     * The player list: hold Tab, read who is on the server, let go.
     *
     * Drawn by the game's own Scaleform renderer in the frame the game uses
     * for its tutorial notes -- the carved gilt border over parchment, built
     * from `tutorial_top_wide`, as many `tutorial_middle_wide` as the list is
     * long, and `tutorial_bottom_wide` -- with the codex's red flourish under
     * the title and the game's own faces and ink. Like the emote wheel it is
     * composed once per mounted screen, so a keypress only shows it.
     *
     * The roster is the server's, not the streamed bodies: a player a
     * kilometre away is on the list too. It is asked for when the session
     * starts, so the first press has something to show, and again every two
     * seconds while the list is up; an answer for a roster the client
     * already holds carries pings alone.
     *
     * The local player is pinned to the top and marked; everyone else is in
     * alphabetical order. Past `kVisibleRows` the mouse wheel scrolls, and
     * while the list is up the wheel's combat binding -- switching opponent
     * -- is held back.
     *
     * Tab is also the game's opponent lock. Out of combat the list opens on
     * the press; in combat it waits `kCombatHoldDelay`, so a tap locks a
     * target without flashing the list.
     *
     * On unless the server's `mod` object sets `scoreboard` to false, which
     * reaches the client in the session config. Then nothing here runs.
     *
     * Game thread only, apart from the network handler's inbox.
     */
    class ScoreboardService final
        : public KCDC::Core::IFeatureService
        , public SDK::IActionListener {
      public:
        /** Rows shown at once; the rest scroll. Sixteen keep the frame inside a 1080-line screen. */
        static constexpr std::size_t kVisibleRows = 16;

        /** How many middle slices the frame can stretch across, which bounds `kVisibleRows`. */
        static constexpr std::size_t kMaxTiles = 8;

        static void Install();

        void Update() override;
        void Reset() override;

        /** Network thread: keeps the newest answer for the next tick. */
        void Receive(const KCDC::Shared::RPC::Scoreboard &answer);

        /** Whether the list is on screen at all, from the press until its fade-out ends. */
        bool IsShown() const {
            return _visible;
        }

        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

      private:
        /** One drawn line of the list, and what it last showed, so an unchanged line costs no call. */
        struct Line {
            std::string id;
            std::string name;
            std::string ping;
            std::string shownId;
            std::string shownName;
            std::string shownPing;

            /** -1 until first styled; then whether the ID and name are drawn as the local player's, and the ping's colour. */
            int shownLocal      = -1;
            int shownPingColour = -1;
        };

        void EnsureScreen();
        bool Compose();
        void ForgetComposition();

        /** The composed page, or null once its movie has gone. */
        std::shared_ptr<KCDC::Core::NativeUI> Page() const;

        /** Takes the newest answer off the inbox and folds it into the roster. */
        void Drain();
        void Apply(KCDC::Shared::RPC::Scoreboard &answer);

        /** Local player first, then by name without regard to case. Only a names answer can change the order. */
        void Sort();

        /** 1 when the local player heads the list and is held there, 0 before the first answer. */
        std::size_t Pinned() const;

        /** The furthest the rows under the pinned one can scroll. */
        std::size_t MaxScroll() const;

        void Request();

        void Open();
        void Close();

        /** Whether the list may be up at all: session, body, controls. */
        bool StillAllowed() const;

        /** Whether a press of the key is also the game's opponent lock right now. */
        static bool InCombat();

        /** Resizes the frame to the rows it holds and writes every line. */
        void Refresh();
        void Layout(KCDC::Core::NativeUI &page, std::size_t shown, bool overflow);
        void WriteLines(KCDC::Core::NativeUI &page);

        void Fade(float elapsedSeconds);

        void AcquireInput();
        void ReleaseInput();

        std::uint32_t _screen = 0;
        bool _composed        = false;
        bool _failed          = false;

        /** Handles the list rewrites after composition; the fixed parts are composed and let go. */
        std::string _root;
        std::array<std::string, kMaxTiles> _tiles;
        std::string _bottom;
        std::string _count;
        std::string _rules;
        std::string _mark;
        std::string _footer;
        std::vector<Line> _lines;

        /** What the frame was last laid out for, so a refresh that changes neither redraws nothing. */
        std::size_t _laidOutRows = static_cast<std::size_t>(-1);
        bool _laidOutOverflow    = false;
        std::string _shownCount;
        std::string _shownFooter;
        int _shownMark = -2;

        /** Stage units per layout unit, and the horizontal correction on top of it; see `Open`. */
        float _unit       = 1.0F;
        float _stageWidth = 1920.0F;

        /** The server's rows as they arrived, names and all, in the order `Sort` puts them. */
        std::vector<KCDC::Shared::RPC::ScoreboardRow> _entries;

        /** The roster or the scroll moved since the lines were last written. */
        bool _dirty               = true;
        std::uint32_t _revision   = 0;
        std::uint16_t _localSlot  = 0;
        std::uint16_t _maxPlayers = 0;

        std::mutex _inboxMutex;
        std::optional<KCDC::Shared::RPC::Scoreboard> _inbox;

        bool _requestedOnce = false;
        std::chrono::steady_clock::time_point _requestedAt {};
        std::chrono::steady_clock::time_point _heldSince {};
        bool _heldLast = false;

        bool _open    = false;
        float _alpha  = 0.0F;
        bool _visible = false;
        std::chrono::steady_clock::time_point _lastTick {};

        /** The first row drawn, and notches of the wheel the engine delivered since the last tick. */
        std::size_t _scroll = 0;
        int _pendingScroll  = 0;

        SDK::IActionMapManager *_inputManager = nullptr;
        SDK::IActionMap *_inputMap            = nullptr;
        SDK::IActionFilter *_filter           = nullptr;
        bool _listening                       = false;
    };
} // namespace KCDC::Features::Social
