#pragma once

namespace SDK::wh::entitymodule {
    /**
     * Native Lua surface installed as the global `EntityModule`.
     *
     * Its methods are registered by 0x1817B32E0 against free thunks taking
     * this object, an `IFunctionHandler` and the Lua arguments; none of them
     * reads a member, so none is mapped and the size is not asserted.
     * `HasPlayerVisibleItems` (0x181F27830) is what AnimStash.lua's open
     * prompt reads to tell an empty container from a full one; the prompt
     * shows "(empty)" when it returns true.
     */
    class C_ScriptBindEntityModule;
} // namespace SDK::wh::entitymodule
