#pragma once

#include "i_game_object.h"

#include <cstdint>

namespace SDK {
    /**
     * What an entity extension implements to own its own aspect profiles.
     *
     * CryAction's `IGameObjectProfileManager`, unchanged: a destructor, a
     * setter and a default. `CGameObject::SetAspectProfile` forwards here for
     * the **network** aspects, of which `eEA_Physics` (8) is one.
     *
     * Warhorse added an aspect that is not a network aspect --
     * `EEntityAspects::Update` (`0x20000000`) -- and `C_Actor`'s implementation
     * of this interface handles it. The game object cannot: its own
     * `SetAspectProfile` indexes the network aspect table by bit, and bit 29 is
     * off the end of it. In this PGO build that lands on the compiler's
     * unreachable stub (`0x181A72980`, a store to address zero), which is an
     * instant access violation rather than a refusal.
     *
     * So an aspect the engine routes through the game object is set on the game
     * object, and `Update` is set here, on the actor's own profile manager.
     */
    class IGameObjectProfileManager {
      public:
        virtual ~IGameObjectProfileManager() = default; // slot 0

        /**
         * Applies one aspect profile. Two arguments, not three: the
         * `fromNetwork` flag belongs to `IGameObject::SetAspectProfile`, which
         * is a different function.
         *
         * `C_Actor`'s implementation (0x18087ACA0) answers exactly two aspects
         * -- `Physics`, which it hands to `C_RagdollManager::SetPhysicsProfile`,
         * and `Update`, which it writes to `C_Actor+0x9B0` and follows with
         * `RefreshUpdateSlot`. Anything else is ignored and answers false.
         */
        virtual bool SetAspectProfile(EEntityAspects aspect, std::uint8_t profile) = 0; // slot 1

        /** The profile an aspect starts on when the entity is spawned. */
        virtual std::uint8_t GetDefaultProfile(EEntityAspects aspect) = 0; // slot 2
    };
} // namespace SDK
