#pragma once

#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * What a fighter is doing, as a bitmask.
     *
     * Named and valued by the game's own RTTR registration of
     * wh::combatmodule::E_CombatActorStateId at 0x1800C9FC0. It is a mask
     * rather than an ordinal -- the enumerators are powers of two and the
     * registration hands RTTR each one individually.
     *
     * It lives in the combat model at +0x030, and it is the only property in
     * that model whose signal carries the previous value as well as the new
     * one: the engine cares about the transition, not just where it landed.
     *
     * Anything that wants to be told when this moves should detour the
     * engine's own subscriber rather than subscribe of its own accord.
     * `C_CombatActorEvents::OnCombatStateChanged` at 0x180911088 is connected
     * to the state property's signal (`model + 0x48`) by every fighter's
     * C_CombatActorEvents constructor, so one detour covers every actor with
     * no subscription to keep alive. Its register contract is
     * `(RCX = the C_CombatActorEvents, RDX = opaque, R8D = previous state,
     * R9D = new state)`, proven by five independently compiled subscribers on
     * that same signal -- 0x180C504E8, 0x180910720, 0x180911250, 0x1819DF2DC
     * and that one -- which between them compare R9D against every value
     * below.
     */
    struct E_CombatActorStateId {
        enum Type : std::uint32_t {
            None             = 0,
            Idle             = 1,
            Guard            = 2,
            ReadingToStrike  = 4,
            Striking         = 8,
            FailedAttack     = 16,
            Withdraw         = 32,
            Hit              = 64,
            PreparingToParry = 128,
            ParryInPlace     = 256,
            Dodge            = 512,
            Transition       = 1024,

            /** Every enumerator the registration declares, OR'd together. */
            All = Idle | Guard | ReadingToStrike | Striking | FailedAttack | Withdraw | Hit | PreparingToParry | ParryInPlace | Dodge | Transition,
        };

        /**
         * Whether this state means the actor is in a fight at all.
         *
         * Idle is the default the property is constructed with, so a body that
         * has never fought reads as Idle rather than as nothing.
         */
        static bool IsFighting(Type state) {
            return (state & ~static_cast<std::uint32_t>(Idle)) != 0;
        }

        /** The enumerator name, or "None"/"Unknown"; for logs and the overlay. */
        static const char *Name(Type state);
    };

    static_assert(sizeof(E_CombatActorStateId::Type) == 0x4, "E_CombatActorStateId::Type size mismatch");

    inline const char *E_CombatActorStateId::Name(Type state) {
        switch (state) {
        case None: return "None";
        case Idle: return "Idle";
        case Guard: return "Guard";
        case ReadingToStrike: return "ReadingToStrike";
        case Striking: return "Striking";
        case FailedAttack: return "FailedAttack";
        case Withdraw: return "Withdraw";
        case Hit: return "Hit";
        case PreparingToParry: return "PreparingToParry";
        case ParryInPlace: return "ParryInPlace";
        case Dodge: return "Dodge";
        case Transition: return "Transition";
        default: return "Unknown";
        }
    }
} // namespace SDK::wh::combatmodule
