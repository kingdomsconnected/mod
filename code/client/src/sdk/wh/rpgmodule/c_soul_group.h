#pragma once

#include "c_soul.h"

#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * A soul's companions.
     *
     * Reached as `I_Soul::GetSoulGroup()` (slot 118). Companions are held in
     * one deque per category at `this + 8 * (5 * category + 9)` -- `+0x48` for
     * horses, `+0x70` for dogs -- and the category is decided by the animal's
     * own kind, not by the caller: soul kind 1 is a horse (category 0), kind 6
     * is a dog (category 1), and everything else is category 2, which
     * `PossessAnimal` refuses.
     *
     * The mod uses only the possess/release pair, and only on a dog. What
     * makes an animal *somebody's* in KCD2 runs through here: the
     * `dog_companion` classification, `GetCompanionMaster`, and the
     * `assaultByDog` / `murderByDog` crime kinds all key on the master this
     * writes onto the animal's own soul.
     */
    class C_SoulGroup {
      public:
        /** The companion category of a dog, and the selector `GetPrimaryCompanion` takes. */
        static constexpr std::uint8_t kCategoryHorse = 0;
        static constexpr std::uint8_t kCategoryDog   = 1;

        /**
         * Makes `animal` a companion of this group's owner.
         *
         * `insertFront` puts it at the head of its category, which is what
         * makes it the *primary* companion -- the one `GetPrimaryCompanion`
         * and the player's dog interactor answer with.
         *
         * Refuses, returning false, when the animal already has a master
         * (`I_Soul::GetCompanionMaster()`), when it is this group's own owner,
         * or when its kind is neither a horse nor a dog. A reassignment is
         * therefore release-then-possess, never possess-over.
         */
        bool PossessAnimal(I_Soul *animal, bool insertFront);

        /**
         * Takes `animal` out of this group and clears the master recorded on
         * its soul. The engine's own release callbacks run inside the call.
         */
        bool ReleaseAnimal(I_Soul *animal);
    };
} // namespace SDK::wh::rpgmodule
