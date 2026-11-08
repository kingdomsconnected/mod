#pragma once

#include "c_script_bind_base.h"

#include <cstddef>

namespace SDK {
    struct IFunctionHandler;
}

namespace SDK::wh::entitymodule {
    class C_Stash;

    /**
     * The Lua surface every stash entity carries as `entity.stash`.
     *
     * Built by the same registrar as `C_ScriptBindLockBase` and shaped the same
     * way. `AttachTo` reads the entity from `C_Stash+0x38` and its id from
     * `C_Stash+0x30`; every handler resolves its `C_Stash` back through
     * `FindStashExtension`.
     *
     * `ReportOpened` and `ReportClosed` are the shipped `AnimStash.lua` telling
     * C++ that the transfer UI opened and closed, and are the only callers of
     * `C_Stash::OnOpened` / `OnClosed` -- which makes that pair the native
     * per-stash open/close event, already sequenced around the game's own
     * robbed-value bookkeeping.
     *
     * Registered as plain function pointers rather than vtable slots, so not
     * declared here: `LoadOriginalModel` (0x181F27930), `LoadPhaseModel`
     * (0x181F27A30), `IsPhaseModelLoaded` (0x181F278F0).
     */
    class C_ScriptBindStash {
      public:
        virtual ~C_ScriptBindStash() = default; // slot 0
        virtual void unk_fn0001() = 0;          // slot 1, shared initializer
        virtual void unk_fn0002() = 0;          // slot 2
        virtual void unk_fn0003() = 0;          // slot 3, empty

        /** Installs `entity.stash` for this extension. */
        virtual void AttachTo(C_Stash *stash) = 0; // slot 4

        /**
         * Lua `stash:ReportOpened(byPlayer)` -> `C_Stash::OnOpened(bool)`.
         * `byPlayer` separates a player open from a scripted one.
         */
        virtual int ReportOpened(IFunctionHandler *handler, bool byPlayer) = 0; // slot 5

        /** Lua `stash:ReportClosed()` -> `C_Stash::OnClosed()`. */
        virtual int ReportClosed(IFunctionHandler *handler) = 0; // slot 6

        /** Lua `stash:DumpItemsToWorld()`; spills the inventory at the entity. */
        virtual int DumpItemsToWorld(IFunctionHandler *handler) = 0; // slot 7

        /** Lua `stash:GetMasterInventory()`; the linked inventory, or null. */
        virtual int GetMasterInventory(IFunctionHandler *handler) = 0; // slot 8

      private:
        ScriptBindPrefixLayout m_prefix; // 0x08
    };

    static_assert(sizeof(C_ScriptBindStash) == 0x70, "C_ScriptBindStash size mismatch");
} // namespace SDK::wh::entitymodule
