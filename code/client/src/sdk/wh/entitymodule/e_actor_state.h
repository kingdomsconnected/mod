#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What a body is doing, as the engine itself describes it.
     *
     * A signalled 45-bit set living on the actor model
     * (`C_ActorModel + 0x370`, words inline at `+0x378`), declared in the
     * binary as `C_ModelBitsetProperty<E_ActorState, std::bitset<45>,
     * C_SignalBitsetTrait<...>>`. Every name and value below is read straight
     * off the game's own RTTR registration at 0x1812BB3A4, so this is the
     * engine's vocabulary rather than a guess at one.
     *
     * It is set by the actor's hierarchical state machine --
     * `C_ActorStateMovement`, `C_ActorStateEntry`,
     * `C_ActorStateAnimationControlled` and friends, all taking
     * `SStateIndex<C_Actor>` and `SStateEvent` in CryEngine's HSM shape -- and
     * read back all over the game. `C_RiderExpansion::CanMount` wants the
     * rider in `Idle`, `Shooting`, `Land` or `Dialogue`; a body in none of
     * them cannot mount a horse at all, whatever is asked of it.
     *
     * **A mod-spawned body with no brain has no state machine driving it**, so
     * this set says what it really is rather than what the mod believes.
     */
    struct E_ActorState {
        enum Type : std::uint8_t {
            Idle                 = 0,
            Jump                 = 1,
            Ledge                = 2,
            Ladder               = 3,
            Minigame             = 4,
            ItemInteraction      = 5,
            DogInteraction       = 6,
            Shooting             = 7,
            Ragdoll              = 8,
            Unconscious          = 9,
            // 10 is registered under a three-character name the strings table
            // does not resolve; left unnamed rather than guessed at.
            CarryCorpse          = 11,
            Carried              = 12,
            CarryItem            = 13,
            StoneThrowing        = 14,
            BattleArcheryPlayer  = 15,
            BattleArcheryNpc     = 16,
            Blacksmithing        = 17,
            Fall                 = 18,
            Land                 = 19,
            StandingUp           = 20,
            Dying                = 21,
            Dead                 = 22,
            Trackview            = 23,
            HoldingDecoy         = 24,
            Combat               = 25,
            Follow               = 26,
            Cart                 = 27,
            Sprinting            = 28,
            InAir                = 29,
            InteractiveScene     = 30,
            Dialogue             = 31,
            SkipTime             = 32,
            FastTravel           = 33,
            Cutscene             = 34,
            TenseCircumstance    = 35,
            Crouch               = 36,
            Horse                = 37,
            OnBed                = 38,
            ActorAnim            = 39,
            Butchering           = 40,
            Reading              = 41,
            ReadingFromInventory = 42,
            Rear                 = 43,
            None                 = 44,
        };

        static constexpr std::uint8_t kCount = 45;
    };
} // namespace SDK::wh::entitymodule
