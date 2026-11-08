#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * What kind of thing an action queued on a `C_ActionActor` is.
     *
     * The whole table, read off `E_ActorActionTypeToString` at `0x180855C20`:
     * the game keeps the names to print them, so every value below is the
     * engine's own spelling rather than a reconstruction. Gaps are values the
     * function has no case for.
     *
     * Two actors are involved in every synchronized animation, and each gets
     * its own action: the driven body takes the `...SyncAnim`/`...Slave`
     * half and the body it is aligned to takes the master half. That is how
     * a rider and a horse, and a player and a dog, end up playing one clip
     * between them.
     */
    struct E_ActorActionType {
        enum Type : std::uint8_t {
            Idle                    = 0x00,
            Rider                   = 0x01,
            RiderAnim               = 0x02,
            RiderIdle               = 0x03,
            HorseAnim               = 0x04,
            CryAnim                 = 0x05,
            TransitionToCombat      = 0x06,
            Fall                    = 0x07,
            Ragdoll                 = 0x08,
            Jump                    = 0x09,
            Land                    = 0x0A,
            StandUp                 = 0x0B,
            Combat                  = 0x0C,
            Hit                     = 0x0D,
            Unconscious             = 0x0E,
            Dead                    = 0x0F,
            Ai                      = 0x10,

            /** A posture: what a dog objective queues to sit, lie or bark. */
            DogAnimAction           = 0x11,

            /** The dog's half of a player-dog synchronized animation. */
            DogSynchroAnimAction    = 0x12,

            AnimSlave               = 0x13,
            WiggleAction            = 0x14,
            GlobalTagAction         = 0x15,
            TrackView               = 0x16,
            Minigame                = 0x17,
            ActorAnimAction         = 0x18,
            ActorAdditiveAnimAction = 0x19,
            Crouch                  = 0x1A,
            Shooting                = 0x1B,
            Ledge                   = 0x1C,
            CarryItem               = 0x1D,
            Follow                  = 0x1E,
            HorseInCart             = 0x1F,
            Cart                    = 0x20,
            CartAnim                = 0x21,
            Ladder                  = 0x22,
            CarryCorpse             = 0x23,
            Carried                 = 0x24,
            InteractiveScene        = 0x25,
            SkipTime                = 0x26,
            FastTravel              = 0x27,
            TenseCircumstance       = 0x28,
            Mount                   = 0x29,
            MountSlave              = 0x2A,
            Dismount                = 0x2B,
            DismountSlave           = 0x2C,
            SyncAnimMaster          = 0x2D,
            SyncAnimSlave           = 0x2E,
            RiderSyncAnimMaster     = 0x2F,

            /** The human's half of that same player-dog animation. */
            DogSyncAnimMaster       = 0x30,

            HorseSyncAnimSlave      = 0x31,
            DropItem                = 0x32,
            PickUpItem              = 0x33,
            PlaceItem               = 0x34,
            DrawWeapons             = 0x35,
            HolsterWeapons          = 0x36,
            SwapWeapons             = 0x37,
            Butchering              = 0x38,
        };
    };

    static_assert(sizeof(E_ActorActionType::Type) == 0x1, "E_ActorActionType::Type size mismatch");
} // namespace SDK::wh::entitymodule
