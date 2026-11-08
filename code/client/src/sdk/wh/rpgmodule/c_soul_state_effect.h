#pragma once

#include "c_soul_value_effect.h"
#include "e_soul_state.h"

namespace SDK::wh::rpgmodule {
    /**
     * The cause the soul state writer is handed: the effect applying the
     * change. `C_SoulStateStorage::ApplyState` (0x18046F3A4) takes it from the
     * applied effect itself, so an event-driven change always carries one,
     * while the direct writers -- regeneration, the mod's respawn heal -- pass
     * none.
     *
     * RTTI: `C_SoulStateEffect : C_SoulValueEffect<E_SoulState, float> :
     * C_SoulEffect : C_Effect`, 0x30 bytes (factory 0x1819A21A4). A write to
     * Health carries the derived `C_HealthValueEffect` instead; see there.
     */
    class C_SoulStateEffect: public C_SoulValueEffect<E_SoulState, float> {};

    static_assert(sizeof(C_SoulStateEffect) == 0x30, "C_SoulStateEffect size mismatch");
} // namespace SDK::wh::rpgmodule
