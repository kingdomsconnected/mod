#pragma once

#include "i_action_filter.h"
#include "i_action_map.h"
#include "i_game_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryAction's action-map manager, reached through
     * IGameFramework::GetIActionMapManager (CCryAction+0x530).
     *
     * KCD2 shuffles this interface; only the slots proven against
     * CActionMapManager's table at 0x184048018 are named:
     *
     *   +0x08  dispatches every enabled action map, but only when the enable
     *          byte at +0xFC is set (the constructor seeds it to 1).
     *   +0xE0  writes that byte, then -- when disabling with reset requested --
     *          walks the action maps releasing everything currently held.
     *   +0xE8  C_UIModalDialogQuestion::OpenQuestionDialog calls it with
     *          ("modal_dialog_question", true) when it opens the dialog.
     */
    class IActionMapManager {
      public:
        virtual ~IActionMapManager() = default;

        virtual void Update() = 0; // +0x08

        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual bool AddExtraActionListener(IActionListener *listener, const char *actionMap)    = 0;
        virtual bool RemoveExtraActionListener(IActionListener *listener, const char *actionMap) = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual IActionMap *CreateActionMap(const char *name)                                    = 0; // Manager owns the returned map.
        virtual bool RemoveActionMap(const char *name)                                           = 0;
        virtual void unk_fn0018() = 0;
        virtual const IActionMap *GetActionMap(const char *name) const                           = 0;
        virtual IActionMap *GetActionMap(const char *name)                                       = 0;
        /**
         * A new, empty, disabled filter of that name, or null when one of
         * that name already exists (0x18079A728).
         *
         * `type` is ignored by this build -- the constructor always stores
         * "action fail" -- and there is no per-filter removal, only
         * `RemoveAllFilters` at +0x1E8.
         */
        virtual IActionFilter *CreateActionFilter(const char *name, int type) = 0; // +0xA8

        virtual IActionFilter *GetActionFilter(const char *name) = 0; // +0xB0
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;

        /** Releases every held action when disabling with resetStateOnDisable. */
        virtual void Enable(bool enable, bool resetStateOnDisable) = 0; // +0xE0
        virtual void EnableActionMap(const char *name, bool enable) = 0; // +0xE8

        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;

        /**
         * Whether one named reason is holding a named filter on.
         *
         * A **query**, despite sitting where stock CryEngine's
         * `EnableFilter(name, bool)` does. KCD2 has no manager-level enable:
         * a filter is turned on through `IActionFilter::Enable`, because the
         * reason has to travel with it.
         */
        virtual bool IsFilterEnabledByReason(const char *name, std::uint8_t reason) = 0; // +0x100

        /** Whether any reason is holding a named filter on; false for a name nothing holds. */
        virtual bool IsFilterEnabled(const char *name) = 0; // +0x108

        /**
         * Releases every action a now-filtered input is still holding.
         * `IActionFilter::Enable` already does this for its own actions, so
         * this is for the wider case.
         */
        virtual void ReleaseFilteredActions() = 0; // +0x110

        /**
         * The filter `name`, loaded from `xml` the first time it is asked
         * for. Kept once made: the only removal on offer is
         * `RemoveAllFilters`, which would take the game's own with it.
         * Loading interns each action name, which is the only way in: a
         * CCryName cannot be built outside the engine. Null when the XML
         * will not parse or load.
         */
        IActionFilter *FindOrCreateActionFilter(const char *name, const char *xml, std::size_t size);

        static IActionMapManager *GetInstance();
    };
} // namespace SDK
