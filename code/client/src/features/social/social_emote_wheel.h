#pragma once

#include "core/feature_registry.h"
#include "core/ui/native_ui/native_ui.h"

#include "sdk/cryengine/action/i_action_filter.h"
#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/system/cry_name.h"
#include "sdk/cryengine/system/i_system.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Social {
    /**
     * The emote wheel: hold the key, point, let go.
     *
     * Drawn by the game's own Scaleform renderer rather than by the debug
     * overlay, because this is a shipping control and has to look like one --
     * and because the overlay is not up in a normal session. The page is
     * composed once, when the session's first screen mounts, and the hold
     * only makes it visible: composing a wheel inside a keypress would put a
     * few hundred ActionScript calls on the frame the player is waiting for.
     *
     * Pointing is the mouse's own delta, taken from an action map of this
     * service's own bound to `maxis_x` and `maxis_y` -- the same two inputs
     * the game's combat direction picker reads -- while an action filter
     * holds back the look actions those inputs otherwise drive. The pointer
     * is a position this service accumulates, not the system cursor: the
     * cursor stays captured, so nothing has to be given back when the wheel
     * closes.
     *
     * Choosing is what the release does, so a hold that ends in the dead zone
     * in the middle chooses nothing. That is the cancel.
     *
     * Which gesture each segment chooses is a layout of `kSlotCount` slots,
     * seeded from `kDefaultSlots` and rearranged by client scripts
     * through `EmoteWheel`. The number of slots is the wheel's, not the
     * catalog's: a catalog that grows gives scripts more to choose from
     * rather than a busier wheel. The layout belongs to the session, so the
     * next server's scripts start from the default one.
     *
     * Game thread only.
     */
    class EmoteWheelService final
        : public KCDC::Core::IFeatureService
        , public SDK::IActionListener {
      public:
        static constexpr std::size_t kSlotCount = 6;

        /**
         * The wheel a session gets when no script touches it: wave, big wave,
         * nod, respectful nod, bow, cheer. Pinned by catalog id rather than
         * taken from the head of the catalog, so rows added to the catalog --
         * wherever the generator puts them -- never move a gesture off the
         * default wheel. The generator fixes each row's id, so these stay put.
         */
        static constexpr std::array<std::uint16_t, kSlotCount> kDefaultSlots {1, 2, 3, 4, 5, 6};

        void Update() override;
        void Reset() override;

        /** The catalog id each slot chooses, clockwise from straight up. */
        const std::array<std::uint16_t, kSlotCount> &GetSlots() const {
            return _slots;
        }

        /** False, and nothing changes, for a slot past the end or an id the catalog does not have. */
        bool SetSlot(std::size_t slot, std::uint16_t emoteId);

        /** Puts every slot back to `kDefaultSlots`. */
        void ResetSlots();

        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

      private:
        /** One drawn segment: which emote it chooses and where its handles are. */
        struct Segment {
            /** The gesture it was last drawn for; a slot that differs is redrawn. */
            std::uint16_t emoteId = 0;
            std::string glyph;
            std::string highlight;
            std::string label;

            /** How the label is anchored, which is decided by the side it is on. */
            std::string align = "center";
        };

        /** Opens and composes the page, and recomposes it if the host unloads. */
        void EnsureScreen();
        bool Compose();

        /**
         * Redraws the segments whose slot no longer matches what they show:
         * the figure is cleared and inked again, the label rewritten. Nothing
         * is created, so a script rearranging the wheel every tick costs a
         * redraw and not a clip, and a tick with nothing changed costs six
         * comparisons.
         */
        void RefreshSlots();

        /** Drops every handle into a page that is gone. */
        void ForgetComposition();

        /** A label's colour is the other half of showing what is selected. */
        bool ApplyLabelStyle(KCDC::Core::NativeUI &page, const Segment &segment, bool lit) const;

        void Open();

        /** `chosen` false cancels; a release inside the dead zone also does. */
        void Close(bool chosen);

        /** Moves the pointer and lights the segment under it. */
        void Track();

        /** The segment the pointer is over, or -1 for the dead zone. */
        int Selection() const;

        bool AcquireInput();
        void ReleaseInput();

        /** Whether the wheel may still be open at all: session, body, controls. */
        bool StillAllowed() const;

        std::uint32_t _screen = 0;
        bool _composed        = false;

        /** Latches a screen that could not be opened or composed; see `EnsureScreen`. */
        bool _failed = false;
        bool _open   = false;

        std::array<std::uint16_t, kSlotCount> _slots = kDefaultSlots;

        std::vector<Segment> _segments;
        std::string _root;
        std::string _backdrop;
        std::string _readout;
        std::string _pointerClip;

        /** Stage units from the middle, which is what a segment is chosen by. */
        float _pointerX = 0.0F;
        float _pointerY = 0.0F;

        /** Accumulated inside the engine's input dispatch, drained by `Track`. */
        float _pendingX = 0.0F;
        float _pendingY = 0.0F;

        int _lit = -1;

        /**
         * The horizontal scale the container was last opened with. Kept only
         * so a change in it is worth one log line, since a wheel that still
         * comes up oval is this number failing to be measured.
         */
        float _correction = 0.0F;

        /** Closes a wheel whose release event never arrived. */
        std::chrono::steady_clock::time_point _openedAt {};

        SDK::IActionMapManager *_inputManager = nullptr;
        SDK::IActionMap *_inputMap            = nullptr;
        SDK::IActionFilter *_filter           = nullptr;
        bool _listening                       = false;
    };
} // namespace KCDC::Features::Social
