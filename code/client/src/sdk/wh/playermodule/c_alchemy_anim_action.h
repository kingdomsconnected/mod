#pragma once

#include "../../cryengine/action/i_mannequin_action.h"
#include "../../patterns.h"

#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * The Mannequin action every alchemy step queues on its player: taking,
     * using, putting and returning a table object, the bellows, the hourglass,
     * the pot and the book (`C_TakeIngredientAction::InitializeAction` and its
     * siblings all construct one). A `C_CallbackAction` underneath (RTTI), with
     * the table's `C_Alchemy` at +0x268: the step passes the one it holds at
     * +0x58 (0x1806C6A7C), which the camera request is read through.
     *
     * Only its identity is typed: the constructor at 0x1806C6CF0 writes the
     * vtable over the `C_CallbackAction` one, and the layout past `IAction` is
     * not modeled.
     */
    class C_AlchemyAnimAction {
      public:
        static bool Is(const IAction &action) {
            return *reinterpret_cast<const std::uint64_t *>(&action) == gPatterns.C_AlchemyAnimAction__VTable;
        }
    };
} // namespace SDK::wh::playermodule
