#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * C_ActionSets secondary base at +0x08, vtable 0x183F60C88: the action
     * help -- every "Press [key] to ..." hint the HUD draws.
     *
     * A hint is a record keyed by (action map, action), loaded from
     * `Libs/Config/defaultActionHelp.xml` by `C_ActionSets::ParseActionHelp`.
     * The HUD shows a map's records while that map is enabled, with the key
     * glyph read from whatever input the action is bound to, so the setters
     * below change a hint in place and never pick a key.
     *
     * Every setter takes the action map first and the action second; the last
     * bool repeats the change on every map that `<include>`s the first. A
     * setter that changes nothing does nothing, and one that changes a record
     * of an enabled map republishes the HUD's list on the spot.
     */
    class I_ActionSets {
      public:
        virtual ~I_ActionSets() = default;
        /** Reads `defaultActionHelp.xml` and registers the sets for action-map events. Once, at player-module init. */
        virtual void LoadDefaultActionHelp()                            = 0;
        virtual void RefreshLocalizedActionHelp(std::uint32_t language) = 0;
        virtual void Opaque03()                                         = 0;
        /** Record +0x40: false draws the hint greyed out. */
        virtual void SetActionEnabled(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool enabled, bool includeDerived) = 0;
        /** Record +0x29: false keeps the record live for the input but draws nothing. */
        virtual void SetActionVisible(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool visible, bool includeDerived) = 0;
        /**
         * Record +0x20 and +0x28: the hint's text, and whether the HUD
         * localizes it. With `localize` false the text reaches the movie as
         * written -- the HUD-hints sink (0x1803C163C) reads +0x28 to choose.
         */
        virtual void SetActionHint(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, const SDK::CryStringT<char> &text, bool localize, bool includeDerived) = 0;
        /** Record +0x48, the disable reason; takes the string by value, so it stays opaque here. */
        virtual void Opaque07() = 0;
        virtual void Opaque08() = 0;
        /** Adds a `std::function<void()>` run when the hint fires; the horse's dismount registers through it. */
        virtual void Opaque09() = 0;
        virtual void Opaque10() = 0;
        /** Clears the named binding, including derived sets when requested. */
        virtual void ClearActionCallbacks(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool includeDerived) = 0;
        /** Record +0x30: how long a Hold hint's ring takes to fill, in seconds. */
        virtual void SetActionHoldDuration(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, float seconds, bool includeDerived) = 0;
        /** Record +0x38, the text shown while holding; takes the string by value, so it stays opaque here. */
        virtual void Opaque13() = 0;
        virtual void Opaque14() = 0;
        virtual void Opaque15() = 0;
        virtual void Opaque16() = 0;
    };

    static_assert(sizeof(I_ActionSets) == 0x08, "I_ActionSets size mismatch");
} // namespace SDK::wh::playermodule
