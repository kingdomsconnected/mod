#pragma once

#include "sdk/wh/playermodule/c_alchemy.h"

#include <cstddef>
#include <cstdint>

namespace KCDC::Features::Crafting {
    /**
     * The capture of the table's back-button lambda. RTTI at 0x183F5F990
     * identifies std::_Func_impl_no_alloc of anonymous lambda
     * b14007554e96de0c368ad4865584e8c0; its constructor
     * (0x1816E9685/0x1816E96A9) stores the vptr and owner, and its copy
     * slot 0x181FFE540 copies exactly these two qwords.
     *
     * Read only inside its Do_call 0x181FFEC90; nothing is retained.
     */
    struct NativeAlchemyBackCapture181FFEC90 {
        std::uintptr_t nativeVtable;
        SDK::wh::playermodule::C_Alchemy *owner;
    };

    static_assert(offsetof(NativeAlchemyBackCapture181FFEC90, nativeVtable) == 0);
    static_assert(offsetof(NativeAlchemyBackCapture181FFEC90, owner) == 0x8);
    static_assert(sizeof(NativeAlchemyBackCapture181FFEC90) == 0x10);
} // namespace KCDC::Features::Crafting
