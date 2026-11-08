#pragma once

#include "../../cryengine/action/i_mannequin_action.h"
#include "../../patterns.h"

#include <cstdint>

namespace SDK::wh::animationmodule {
    /**
     * The Mannequin action the game's own action layer queues.
     *
     * Every fragment the wh action layer starts -- stance transitions,
     * item-handling and interaction animations -- is one of these, queued
     * through `CActionController::Queue` (the controller's slot 19). The mod's
     * own playback queues plain `TAction`s instead, which is what tells the
     * game's animations apart from ones the mod put there.
     *
     * Only its identity is typed: the constructor at 0x180682DA8 writes the
     * vtable, and the layout past `IAction` is not modeled.
     */
    class C_CallbackAction {
      public:
        static bool Is(const IAction &action) {
            return *reinterpret_cast<const std::uint64_t *>(&action) == gPatterns.C_CallbackAction__VTable;
        }
    };
} // namespace SDK::wh::animationmodule
